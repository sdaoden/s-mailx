/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Spam-checker related facilities.
 *
 * Copyright (c) 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#include "config.h"

#ifndef HAVE_SPAM
typedef int avoid_empty_file_compiler_warning;
#else
#include "rcv.h"

#include <unistd.h>

#include "extern.h"

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
 * TODO - We do not support learning yet (neither report nor revoke).
 * TODO - We do not yet handle direct communication with spamd(1).
 * TODO   I.e., this could be a lean alternative to the first item;
 * TODO   the protocol is easy and we could support ALL operations easily.
 * TODO - We do not yet update mails in place, i.e., replace the original
 * TODO   message with the updated one; we could easily do it as if `edit' has
 * TODO   been used, but the nail codebase doesn't truly support that for IMAP.
 * TODO   (And it seems a bit grazy to download a message, update it and upload
 * TODO   it again.)
 */

struct spam_vc {
   struct message *  mp;
   size_t            mno;
   char *            comm_s;
   char const *      comm_a[10];
   char *            buffer;
   /* TODO This codebase jumps around and uses "stacks" of signal handling;
    * TODO until some later time we have to play the same game */
   sighandler_type   otstp;
   sighandler_type   ottin;
   sighandler_type   ottou;
   sighandler_type   ohup;
   sighandler_type   opipe;
   sighandler_type   oint;
};

/* Convert a 2[.x]/whatever spam rate into message.m_spamscore */
static void    _spam_rate2score(struct spam_vc *vc);

static bool_t  _spam_rate(struct spam_vc *vc);

static void
_spam_rate2score(struct spam_vc *vc)
{
   char *cp;
   size_t size;
   ui_it m, s;
   
   cp = strchr(vc->buffer, '/');
   if (cp == NULL)
      goto jleave;
   size = (size_t)(cp - vc->buffer);
   vc->buffer[size] = '\0';

   m = (ui_it)strtol(vc->buffer, &cp, 10);
   if (cp == vc->buffer)
      goto jleave;

   s = (*cp == '\0') ? 0 : (ui_it)strtol(++cp, NULL, 10);

   vc->mp->m_spamscore = (m << 8) | (s & 0xFF);
jleave:
   ;
}

static sigjmp_buf __spam_actjmp; /* TODO someday, we won't need it no more */
static int __spam_sig; /* TODO someday, we won't need it no more */
static void
__spam_onsig(int sig) /* TODO someday, we won't need it no more */
{
   __spam_sig = sig;
   siglongjmp(__spam_actjmp, 1);
}

static bool_t
_spam_rate(struct spam_vc *vc)
{
   char *cp;
   int p2c[2], c2p[2];
   sigset_t cset;
   size_t size;
   pid_t pid;
   FILE *ibuf;
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

   setdot(vc->mp);
   if ((ibuf = setinput(&mb, vc->mp, NEED_BODY)) == NULL) {
      perror("setinput"); /* XXX tr() */
      goto j_leave;
   }

   /* TODO Avoid that we jump away; yet necessary signal mess */
   vc->otstp = safe_signal(SIGTSTP, SIG_DFL);
   vc->ottin = safe_signal(SIGTTIN, SIG_DFL);
   vc->ottou = safe_signal(SIGTTOU, SIG_DFL);
   vc->opipe = safe_signal(SIGPIPE, SIG_IGN);
   holdsigs();
   state |= _SIGHOLD;
   vc->ohup = safe_signal(SIGHUP, &__spam_onsig);
   vc->oint = safe_signal(SIGINT, &__spam_onsig);
   /* Keep sigs blocked */

   if (! pipe_cloexec(p2c)) {
      perror("pipe"); /* XXX tr() */
      goto jleave;
   }
   state |= _P2C;

   if (! pipe_cloexec(c2p)) {
      perror("pipe"); /* XXX tr() */
      goto jleave;
   }
   state |= _C2P;

   if (sigsetjmp(__spam_actjmp, 1)) {
      state |= _JUMPED;
      goto jleave;
   }
   relsesigs();
   state &= ~_SIGHOLD;

   sigemptyset(&cset);
   pid = start_command(vc->comm_s, &cset, p2c[0], c2p[1], NULL, NULL, NULL);
   state |= _RUNNING;
   close(p2c[0]);
   state &= ~_P2C_0;

   /* Yes, we could send(SEND_MBOX), but the simply passing through the MBOX
    * content does the same in effect, but is much more efficient */
   for (cp = vc->buffer, size = vc->mp->m_size; size > 0;) {
      size_t i = fread(vc->buffer, 1, MIN(size, BUFFER_SIZE), ibuf);
      if (i == 0) {
         if (ferror(ibuf))
            state |= _ERRORS;
         break;
      }
      size -= i;
      if (i != (size_t)write(p2c[1], vc->buffer, i)) {
         state |= _ERRORS;
         break;
      }
   }

jleave:
   /* In what follows you see a lot of races; these can't be helped without
    * atomic compare-and-swap; it only matters if we */
   if (state & _SIGHOLD) {
      state &= ~_SIGHOLD;
      relsesigs();
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
      if (wait_child(pid) == 0)
         state |= _GOODRUN;
   }

   /* XXX This only works because spamc(1) follows the clear protocol (1) read
    * XXX everything until EOF on input, then (2) work, then (3) output
    * XXX a single result line; otherwise we could deadlock here, but since
    * TODO this is rather intermediate, go with it */
   if (! (state & (_JUMPED | _ERRORS))) {
      ssize_t i = read(c2p[0], vc->buffer, BUFFER_SIZE - 1);
      if (i > 0) {
         vc->buffer[i] = '\0';
         _spam_rate2score(vc);
      } else
         state |= _ERRORS;
   }

   if (state & _C2P_0) {
      state &= ~_C2P_0;
      close(c2p[0]);
   }

   switch (state & (_JUMPED | _GOODRUN | _ERRORS)) {
   case _GOODRUN:
      vc->mp->m_flag &= ~MSPAM;
      break;
   case 0:
      vc->mp->m_flag |= MSPAM;
   default:
      break;
   }

   safe_signal(SIGINT, vc->oint);
   safe_signal(SIGHUP, vc->ohup);
   safe_signal(SIGPIPE, vc->opipe);
   safe_signal(SIGTSTP, vc->otstp);
   safe_signal(SIGTTIN, vc->ottin);
   safe_signal(SIGTTOU, vc->ottou);

   /* Bounce jumps to the lex.c trampolines */
   if (state & _JUMPED) {
      sigemptyset(&cset);
      sigaddset(&cset, __spam_sig);
      sigprocmask(SIG_UNBLOCK, &cset, NULL);
      kill(0, __spam_sig);
   }
j_leave:
   return ! (state & _ERRORS);
}

int
cspam_rate(void *v)
{
   struct spam_vc vc;
   struct str str;
   int *ip;
   size_t maxsize;
   char const *cp, **args;
   bool_t ok = FAL0;

   if ((cp = voption("spamrate-command")) == NULL) {
#ifdef SPAMC_PATH
      cp = SPAMC_PATH;
#else
      fprintf(stderr, tr(514, "`spamrate': *spamrate-command* is not set\n"));
      goto jleave;
#endif
   }

   args = vc.comm_a;
   args[0] = cp;
   args[1] = "-c";
   args[2] = "-l";
   args += 3;

   if ((cp = voption("spamrate-socket")) != NULL) {
      *args++ = "-U";
      *args++ = cp;
   } else {
      if ((cp = voption("spamrate-host")) != NULL) {
         *args++ = "-d";
         *args++ = cp;
      }
      if ((cp = voption("spamrate-port")) != NULL) {
         *args++ = "-p";
         *args++ = cp;
      }
   }
   *args = NULL;
   vc.comm_s = str_concat_cpa(&str, vc.comm_a, " ")->s;
   vc.buffer = salloc(BUFFER_SIZE);

   maxsize = 0;
   if ((cp = voption("spamrate-maxsize")) != NULL)
      maxsize = (size_t)strtol(cp, NULL, 10);
   if (maxsize <= 0)
      maxsize = SPAM_MAXSIZE;

   for (ok = TRU1, ip = v; *ip != 0; ++ip) {
      vc.mno = (size_t)*ip - 1;
      vc.mp = message + vc.mno;
      vc.mp->m_spamscore = 0;
      if (vc.mp->m_size > maxsize) {
         if (options & OPT_VERBOSE)
            fprintf(stderr, tr(515,
               "`spamrate': message %lu exceeds maxsize (%lu > %lu), skip\n"),
               (ul_it)vc.mno + 1, (ul_it)vc.mp->m_size, (ul_it)maxsize);
         continue;
      }
      if ((ok = _spam_rate(&vc)) == FAL0)
         break;
   }
#ifndef SPAMC_PATH
jleave:
#endif
   return (ok == FAL0) ? STOP : OKAY;
}

int
cspam_set(void *v)
{
   int *ip;

   for (ip = v; *ip != 0; ++ip)
      message[(size_t)*ip - 1].m_flag |= MSPAM;
   return OKAY;
}

int
cspam_clear(void *v)
{
   int *ip;

   for (ip = v; *ip != 0; ++ip)
      message[(size_t)*ip - 1].m_flag &= ~MSPAM;
   return OKAY;
}
#endif /* HAVE_SPAM */

/* vim:set fenc=utf-8:s-it-mode */
