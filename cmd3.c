/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Still more user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
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
#define n_FILE cmd3

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

static char                *_bang_buf;
static size_t              _bang_size;

/* Modify subject we reply to to begin with Re: if it does not already */
static char *     _reedit(char *subj);

/* Expand the shell escape by expanding unescaped !'s into the last issued
 * command where possible */
static void       _bangexp(char **str, size_t *size);

static void       make_ref_and_cs(struct message *mp, struct header *head);

/* `reply' and `Lreply' workhorse */
static int        _list_reply(int *msgvec, enum header_flags hf);

/* Get PTF to implementation of command c (i.e., take care for *flipr*) */
static int (*     _reply_or_Reply(char c))(int *, bool_t);

/* Reply to a single message.  Extract each name from the message header and
 * send them off to mail1() */
static int        _reply(int *msgvec, bool_t recipient_record);

/* Reply to a series of messages by simply mailing to the senders and not
 * messing around with the To: and Cc: lists as in normal reply */
static int        _Reply(int *msgvec, bool_t recipient_record);

/* Forward a message to a new recipient, in the sense of RFC 2822 */
static int        _fwd(char *str, int recipient_record);

/* Modify the subject we are replying to to begin with Fwd: */
static char *     __fwdedit(char *subj);

/* Do the real work of resending */
static int        _resend1(void *v, bool_t add_resent);

/* c_file, c_File */
static int        _c_file(void *v, enum fedit_mode fm);

static char *
_reedit(char *subj)
{
   struct str in, out;
   char *newsubj = NULL;
   NYD_ENTER;

   if (subj == NULL || *subj == '\0')
      goto jleave;

   in.s = subj;
   in.l = strlen(subj);
   mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);

   if ((newsubj = subject_re_trim(out.s)) != out.s)
      newsubj = savestr(out.s);
   else {
      /* RFC mandates english "Re: " */
      newsubj = salloc(out.l + 4 +1);
      sstpcpy(sstpcpy(newsubj, "Re: "), out.s);
   }

   free(out.s);
jleave:
   NYD_LEAVE;
   return newsubj;
}

static void
_bangexp(char **str, size_t *size)
{
   char *bangbuf;
   int changed = 0;
   bool_t dobang;
   size_t sz, i, j, bangbufsize;
   NYD_ENTER;

   dobang = ok_blook(bang);

   bangbuf = smalloc(bangbufsize = *size);
   i = j = 0;
   while ((*str)[i]) {
      if (dobang) {
         if ((*str)[i] == '!') {
            sz = strlen(_bang_buf);
            bangbuf = srealloc(bangbuf, bangbufsize += sz);
            ++changed;
            memcpy(bangbuf + j, _bang_buf, sz + 1);
            j += sz;
            i++;
            continue;
         }
      }
      if ((*str)[i] == '\\' && (*str)[i + 1] == '!') {
         bangbuf[j++] = '!';
         i += 2;
         ++changed;
      }
      bangbuf[j++] = (*str)[i++];
   }
   bangbuf[j] = '\0';
   if (changed) {
      printf("!%s\n", bangbuf);
      fflush(stdout);
   }
   sz = j + 1;
   if (sz > *size)
      *str = srealloc(*str, *size = sz);
   memcpy(*str, bangbuf, sz);
   if (sz > _bang_size)
      _bang_buf = srealloc(_bang_buf, _bang_size = sz);
   memcpy(_bang_buf, bangbuf, sz);
   free(bangbuf);
   NYD_LEAVE;
}

static void
make_ref_and_cs(struct message *mp, struct header *head) /* TODO rewrite FAST */
{
   char *oldref, *oldmsgid, *newref, *cp;
   size_t oldreflen = 0, oldmsgidlen = 0, reflen;
   unsigned i;
   struct name *n;
   NYD_ENTER;

   oldref = hfield1("references", mp);
   oldmsgid = hfield1("message-id", mp);
   if (oldmsgid == NULL || *oldmsgid == '\0') {
      head->h_ref = NULL;
      goto jleave;
   }

   reflen = 1;
   if (oldref) {
      oldreflen = strlen(oldref);
      reflen += oldreflen + 2;
   }
   if (oldmsgid) {
      oldmsgidlen = strlen(oldmsgid);
      reflen += oldmsgidlen;
   }

   newref = smalloc(reflen);
   if (oldref != NULL) {
      memcpy(newref, oldref, oldreflen +1);
      if (oldmsgid != NULL) {
         newref[oldreflen++] = ',';
         newref[oldreflen++] = ' ';
         memcpy(newref + oldreflen, oldmsgid, oldmsgidlen +1);
      }
   } else if (oldmsgid)
      memcpy(newref, oldmsgid, oldmsgidlen +1);
   n = extract(newref, GREF);
   free(newref);

   /* Limit number of references TODO better on parser side */
   while (n->n_flink != NULL)
      n = n->n_flink;
   for (i = 1; i <= REFERENCES_MAX; ++i) {
      if (n->n_blink != NULL)
         n = n->n_blink;
      else
         break;
   }
   n->n_blink = NULL;
   head->h_ref = n;
   if (ok_blook(reply_in_same_charset) &&
         (cp = hfield1("content-type", mp)) != NULL)
      head->h_charset = mime_param_get("charset", cp);
jleave:
   NYD_LEAVE;
}

static int
_list_reply(int *msgvec, enum header_flags hf)
{
   struct header head;
   struct message *mp;
   char const *reply_to, *rcv, *cp;
   enum gfield gf;
   struct name *rt, *mft, *np;
   int *save_msgvec;
   NYD_ENTER;

   /* TODO Since we may recur and do stuff with message lists we need to save
    * TODO away the argument vector as long as that isn't done by machinery */
   {
      size_t i;
      for (i = 0; msgvec[i] != 0; ++i)
         ;
      ++i;
      save_msgvec = ac_alloc(sizeof(*save_msgvec) * i);
      while (i-- > 0)
         save_msgvec[i] = msgvec[i];
      msgvec = save_msgvec;
   }

jnext_msg:
   mp = message + *msgvec - 1;
   touch(mp);
   setdot(mp);

   memset(&head, 0, sizeof head);
   head.h_flags = hf;

   head.h_subject = _reedit(hfield1("subject", mp));
   gf = ok_blook(fullnames) ? GFULL : GSKIN;
   rt = mft = NULL;

   rcv = NULL;
   if ((reply_to = hfield1("reply-to", mp)) != NULL &&
         (cp = ok_vlook(reply_to_honour)) != NULL &&
         (rt = checkaddrs(lextract(reply_to, GTO | gf), EACM_STRICT, NULL)
         ) != NULL) {
      char const *tr = _("Reply-To %s%s");
      size_t l = strlen(tr) + strlen(rt->n_name) + 3 +1;
      char *sp = salloc(l);

      snprintf(sp, l, tr, rt->n_name, (rt->n_flink != NULL ? "..." : ""));
      if (quadify(cp, UIZ_MAX, sp, TRU1) > FAL0)
         rcv = reply_to;
   }

   if (rcv == NULL && (rcv = hfield1("from", mp)) == NULL)
      rcv = nameof(mp, 1);

   /* Cc: */
   np = NULL;
   if (ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL)
      np = lextract(cp, GCC | gf);
   if ((cp = hfield1("cc", mp)) != NULL)
      np = cat(np, lextract(cp, GCC | gf));
   if (np != NULL)
      head.h_cc = delete_alternates(np);

   /* To: */
   np = NULL;
   if (rcv != NULL)
      np = (rcv == reply_to) ? namelist_dup(rt, GTO | gf)
            : lextract(rcv, GTO | gf);
   if (!ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL)
      np = cat(np, lextract(cp, GTO | gf));
   /* Delete my name from reply list, and with it, all my alternate names */
   np = delete_alternates(np);
   if (count(np) == 0)
      np = lextract(rcv, GTO | gf);
   head.h_to = np;

   /* The user may have send this to himself, don't ignore that */
   namelist_vaporise_head(&head, EACM_NORMAL, FAL0, NULL);
   if (head.h_to == NULL)
      head.h_to = np;

   /* Mail-Followup-To: */
   mft = NULL;
   if (ok_vlook(followup_to_honour) != NULL &&
         (cp = hfield1("mail-followup-to", mp)) != NULL &&
         (mft = np = checkaddrs(lextract(cp, GTO | gf), EACM_STRICT, NULL)
         ) != NULL) {
      char const *tr = _("Followup-To %s%s");
      size_t l = strlen(tr) + strlen(np->n_name) + 3 +1;
      char *sp = salloc(l);

      snprintf(sp, l, tr, np->n_name, (np->n_flink != NULL ? "..." : ""));
      if (quadify(ok_vlook(followup_to_honour), UIZ_MAX, sp, TRU1) > FAL0) {
         head.h_cc = NULL;
         head.h_to = np;
         head.h_mft =
         mft = namelist_vaporise_head(&head, EACM_STRICT, FAL0, NULL);
      } else
         mft = NULL;
   }

   /* Special massage for list (follow-up) messages */
   if (mft != NULL || (hf & HF_LIST_REPLY) || ok_blook(followup_to)) {
      /* Learn about a possibly sending mailing list; use do for break; */
      if ((cp = hfield1("list-post", mp)) != NULL) do {
         struct name *x;

         if ((x = lextract(cp, GEXTRA | GSKIN)) == NULL || x->n_flink != NULL ||
               (cp = url_mailto_to_address(x->n_name)) == NULL ||
               /* XXX terribly wasteful to create a new name, and can't we find
                * XXX a way to mitigate that?? */
               is_addr_invalid(x = nalloc(cp, GEXTRA | GSKIN), EACM_STRICT)) {
            if (options & OPT_D_V)
               n_err(_("Message contains invalid List-Post: header\n"));
            cp = NULL;
            break;
         }
         cp = x->n_name;

         /* A special case has been seen on e.g. ietf-announce@ietf.org:
          * these usually post to multiple groups, with ietf-announce@
          * in List-Post:, but with Reply-To: set to ietf@ietf.org (since
          * -announce@ is only used for announcements, say).
          * So our desire is to honour this request and actively overwrite
          * List-Post: for our purpose; but only if its a single address.
          * However, to avoid ambiguities with users that place themselve in
          * Reply-To: and mailing lists which don't overwrite this (or only
          * extend this, shall such exist), only do so if reply_to exists of
          * a single address which points to the same domain as List-Post: */
         if (reply_to != NULL && rt->n_flink == NULL &&
               name_is_same_domain(x, rt))
            cp = rt->n_name; /* rt is EACM_STRICT tested */

         /* "Automatically `mlist'" the List-Post: address temporarily */
         if (is_mlist(cp, FAL0) == MLIST_OTHER)
            head.h_list_post = cp;
         else
            cp = NULL;
      } while (0);

      /* In case of list replies we actively sort out any non-list recipient,
       * but _only_ if we did not honour a MFT:, assuming that members of MFT
       * were there for a reason; cp is still List-Post:/eqivalent */
      if ((hf & HF_LIST_REPLY) && mft == NULL) {
         struct name *nhp = head.h_to;
         head.h_to = NULL;
j_lt_redo:
         while (nhp != NULL) {
            np = nhp;
            nhp = nhp->n_flink;

            if ((cp != NULL && !asccasecmp(cp, np->n_name)) ||
                  is_mlist(np->n_name, FAL0) != MLIST_OTHER) {
               np->n_type = (np->n_type & ~GMASK) | GTO;
               np->n_flink = head.h_to;
               head.h_to = np;
            }
         }
         if ((nhp = head.h_cc) != NULL) {
            head.h_cc = NULL;
            goto j_lt_redo;
         }
      }
   }

   make_ref_and_cs(mp, &head);

   if (ok_blook(quote_as_attachment)) {
      head.h_attach = csalloc(1, sizeof *head.h_attach);
      head.h_attach->a_msgno = *msgvec;
      head.h_attach->a_content_description = _("Original message content");
   }

   if (mail1(&head, 1, mp, NULL, !!(hf & HF_RECIPIENT_RECORD), 0) == OKAY &&
         ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;

   if (*++msgvec != 0) {
      /* TODO message (error) ring.., less sleep */
      printf(_("Waiting a second before proceeding to the next message..\n"));
      fflush(stdout);
      n_msleep(1000, FAL0);
      goto jnext_msg;
   }

   ac_free(save_msgvec);
   NYD_LEAVE;
   return 0;
}

static int
(*_reply_or_Reply(char c))(int *, bool_t)
{
   int (*rv)(int*, bool_t);
   NYD_ENTER;

   rv = (ok_blook(flipr) ^ (c == 'R')) ? &_Reply : &_reply;
   NYD_LEAVE;
   return rv;
}

static int
_reply(int *msgvec, bool_t recipient_record)
{
   int rv;
   NYD_ENTER;

   rv = _list_reply(msgvec, recipient_record ? HF_RECIPIENT_RECORD : HF_NONE);
   NYD_LEAVE;
   return rv;
}

static int
_Reply(int *msgvec, bool_t recipient_record)
{
   struct header head;
   struct message *mp;
   int *ap;
   char *cp;
   enum gfield gf;
   NYD_ENTER;

   memset(&head, 0, sizeof head);
   gf = ok_blook(fullnames) ? GFULL : GSKIN;

   for (ap = msgvec; *ap != 0; ++ap) {
      char const *rp;
      struct name *rt;

      mp = message + *ap - 1;
      touch(mp);
      setdot(mp);

      if ((rp = hfield1("reply-to", mp)) != NULL &&
            (cp = ok_vlook(reply_to_honour)) != NULL &&
            (rt = checkaddrs(lextract(rp, GTO | gf), EACM_STRICT, NULL)
            ) != NULL) {
         char const *tr = _("Reply-To %s%s");
         size_t l = strlen(tr) + strlen(rt->n_name) + 3 +1;
         char *sp = salloc(l);

         snprintf(sp, l, tr, rt->n_name, (rt->n_flink != NULL ? "..." : ""));
         if (quadify(cp, UIZ_MAX, sp, TRU1) > FAL0) {
            head.h_to = cat(head.h_to, rt);
            continue;
         }
      }

      if ((cp = hfield1("from", mp)) == NULL)
         cp = nameof(mp, 2);
      head.h_to = cat(head.h_to, lextract(cp, GTO | gf));
   }
   if (head.h_to == NULL)
      goto jleave;

   mp = message + msgvec[0] - 1;
   head.h_subject = hfield1("subject", mp);
   head.h_subject = _reedit(head.h_subject);
   make_ref_and_cs(mp, &head);

   if (ok_blook(quote_as_attachment)) {
      head.h_attach = csalloc(1, sizeof *head.h_attach);
      head.h_attach->a_msgno = *msgvec;
      head.h_attach->a_content_description = _("Original message content");
   }

   if (mail1(&head, 1, mp, NULL, recipient_record, 0) == OKAY &&
         ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;
jleave:
   NYD_LEAVE;
   return 0;
}

static int
_fwd(char *str, int recipient_record)
{
   struct header head;
   int *msgvec, rv = 1;
   char *recipient;
   struct message *mp;
   bool_t f, forward_as_attachment;
   NYD_ENTER;

   if ((recipient = laststring(str, &f, TRU1)) == NULL) {
      puts(_("No recipient specified."));
      goto jleave;
   }

   forward_as_attachment = ok_blook(forward_as_attachment);
   msgvec = salloc((msgCount + 2) * sizeof *msgvec);

   if (!f) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (pstate & (PS_HOOK_MASK | PS_ROBOT)) {
            rv = 0;
            goto jleave;
         }
         printf(_("No messages to forward.\n"));
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;

   if (*msgvec == 0) {
      if (pstate & (PS_HOOK_MASK | PS_ROBOT)) {
         rv = 0;
         goto jleave;
      }
      printf(_("No applicable messages.\n"));
      goto jleave;
   }
   if (msgvec[1] != 0) {
      printf(_("Cannot forward multiple messages at once\n"));
      goto jleave;
   }

   memset(&head, 0, sizeof head);
   if ((head.h_to = lextract(recipient,
         (GTO | (ok_blook(fullnames) ? GFULL : GSKIN)))) == NULL)
      goto jleave;

   mp = message + *msgvec - 1;

   if (forward_as_attachment) {
      head.h_attach = csalloc(1, sizeof *head.h_attach);
      head.h_attach->a_msgno = *msgvec;
      head.h_attach->a_content_description = _("Forwarded message");
   } else {
      touch(mp);
      setdot(mp);
   }
   head.h_subject = hfield1("subject", mp);
   head.h_subject = __fwdedit(head.h_subject);
   mail1(&head, 1, (forward_as_attachment ? NULL : mp), NULL, recipient_record,
      1);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static char *
__fwdedit(char *subj)
{
   struct str in, out;
   char *newsubj = NULL;
   NYD_ENTER;

   if (subj == NULL || *subj == '\0')
      goto jleave;

   in.s = subj;
   in.l = strlen(subj);
   mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);

   newsubj = salloc(out.l + 6);
   memcpy(newsubj, "Fwd: ", 5); /* XXX localizable */
   memcpy(newsubj + 5, out.s, out.l +1);
   free(out.s);
jleave:
   NYD_LEAVE;
   return newsubj;
}

static int
_resend1(void *v, bool_t add_resent)
{
   char *name, *str;
   struct name *to, *sn;
   int *ip, *msgvec;
   bool_t f = TRU1;
   NYD_ENTER;

   str = v;
   msgvec = salloc((msgCount + 2) * sizeof *msgvec);
   name = laststring(str, &f, TRU1);
   if (name == NULL) {
      puts(_("No recipient specified."));
      goto jleave;
   }

   if (!f) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (pstate & (PS_HOOK_MASK | PS_ROBOT)) {
            f = FAL0;
            goto jleave;
         }
         puts(_("No applicable messages."));
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;

   if (*msgvec == 0) {
      if (pstate & (PS_HOOK_MASK | PS_ROBOT)) {
         f = FAL0;
         goto jleave;
      }
      printf("No applicable messages.\n");
      goto jleave;
   }

   sn = nalloc(name, GTO | GSKIN);
   to = usermap(sn, FAL0);
   for (ip = msgvec; *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount);
         ++ip)
      if (resend_msg(message + *ip - 1, to, add_resent) != OKAY)
         goto jleave;
   f = FAL0;
jleave:
   NYD_LEAVE;
   return (f != FAL0);
}

static int
_c_file(void *v, enum fedit_mode fm)
{
   char **argv = v;
   int i;
   NYD2_ENTER;

   if (*argv == NULL) {
      newfileinfo();
      i = 0;
      goto jleave;
   }

   if (pstate & PS_HOOK_MASK) {
      n_err(_("Cannot change folder from within a hook\n"));
      i = 1;
      goto jleave;
   }

   save_mbox_for_possible_quitstuff();

   i = setfile(*argv, fm);
   if (i < 0) {
      i = 1;
      goto jleave;
   }
   assert(!(fm & FEDIT_NEWMAIL)); /* (Prevent implementation error) */
   if (pstate & PS_SETFILE_OPENED)
      check_folder_hook(FAL0);

   if (i > 0) {
      /* TODO Don't report "no messages" == 1 == error when we're in, e.g.,
       * TODO a macro: because that recursed commando loop will terminate the
       * TODO entire macro due to that!  So either the user needs to be able
       * TODO to react&ignore this "error" (as in "if DOSTUFF" or "DOSTUFF;
       * TODO if $?", then "overriding an "error"), or we need a different
       * TODO return that differentiates */
      i = (pstate & PS_ROBOT) ? 0 : 1;
      goto jleave;
   }
   if (pstate & PS_SETFILE_OPENED)
      announce(ok_blook(bsdcompat) || ok_blook(bsdannounce));
   i = 0;
jleave:
   NYD2_LEAVE;
   return i;
}

FL int
c_shell(void *v)
{
   char *str = v, *cmd;
   size_t cmdsize;
   sigset_t mask;
   sighandler_type sigint;
   NYD_ENTER;

   cmd = smalloc(cmdsize = strlen(str) +1);
   memcpy(cmd, str, cmdsize);
   _bangexp(&cmd, &cmdsize);

   sigint = safe_signal(SIGINT, SIG_IGN);
   sigemptyset(&mask);
   run_command(ok_vlook(SHELL), &mask, COMMAND_FD_PASS, COMMAND_FD_PASS, "-c",
      cmd, NULL, NULL);
   safe_signal(SIGINT, sigint);
   printf("!\n");

   free(cmd);
   NYD_LEAVE;
   return 0;
}

FL int
c_dosh(void *v)
{
   sighandler_type sigint;
   NYD_ENTER;
   UNUSED(v);

   sigint = safe_signal(SIGINT, SIG_IGN);
   run_command(ok_vlook(SHELL), 0, COMMAND_FD_PASS, COMMAND_FD_PASS, NULL,
      NULL, NULL, NULL);
   safe_signal(SIGINT, sigint);
   putchar('\n');
   NYD_LEAVE;
   return 0;
}

FL int
c_cwd(void *v)
{
   char buf[PATH_MAX]; /* TODO getcwd(3) may return a larger value */
   NYD_ENTER;

   if (getcwd(buf, sizeof buf) != NULL) {
      puts(buf);
      v = (void*)0x1;
   } else {
      n_perr(_("getcwd"), 0);
      v = NULL;
   }
   NYD_LEAVE;
   return (v == NULL);
}

FL int
c_chdir(void *v)
{
   char **arglist = v;
   char const *cp;
   NYD_ENTER;

   if (*arglist == NULL)
      cp = ok_vlook(HOME);
   else if ((cp = file_expand(*arglist)) == NULL)
      goto jleave;
   if (chdir(cp) == -1) {
      n_perr(cp, 0);
      cp = NULL;
   }
jleave:
   NYD_LEAVE;
   return (cp == NULL);
}

FL int
c_reply(void *v)
{
   int rv;
   NYD_ENTER;

   rv = (*_reply_or_Reply('r'))(v, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_replyall(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _reply(v, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_replysender(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _Reply(v, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Reply(void *v)
{
   int rv;
   NYD_ENTER;

   rv = (*_reply_or_Reply('R'))(v, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Lreply(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _list_reply(v, HF_LIST_REPLY);
   NYD_LEAVE;
   return rv;
}

FL int
c_followup(void *v)
{
   int rv;
   NYD_ENTER;

   rv = (*_reply_or_Reply('r'))(v, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_followupall(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _reply(v, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_followupsender(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _Reply(v, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Followup(void *v)
{
   int rv;
   NYD_ENTER;

   rv = (*_reply_or_Reply('R'))(v, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_forward(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _fwd(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Forward(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _fwd(v, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_resend(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _resend1(v, TRU1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Resend(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _resend1(v, FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
c_messize(void *v)
{
   int *msgvec = v, *ip, mesg;
   struct message *mp;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      mesg = *ip;
      mp = message + mesg - 1;
      printf("%d: ", mesg);
      if (mp->m_xlines > 0)
         printf("%ld", mp->m_xlines);
      else
         putchar(' ');
      printf("/%lu\n", (ul_i)mp->m_xsize);
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_file(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _c_file(v, FEDIT_NONE);
   NYD_LEAVE;
   return rv;
}

FL int
c_File(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _c_file(v, FEDIT_RDONLY);
   NYD_LEAVE;
   return rv;
}

FL int
c_echo(void *v){
   char const **argv, **ap, *cp;
   NYD_ENTER;

   for(ap = argv = v; *ap != NULL; ++ap){
      if(ap != argv)
         putchar(' ');
      if((cp = fexpand(*ap, FEXP_NSHORTCUT | FEXP_NVAR)) == NULL)
         cp = *ap;
      fputs(cp, stdout);
   }
   putchar('\n');
   NYD_LEAVE;
   return 0;
}

FL int
c_newmail(void *v)
{
   int val = 1, mdot;
   NYD_ENTER;
   UNUSED(v);

   if ((val = setfile(mailname,
            FEDIT_NEWMAIL | ((mb.mb_perm & MB_DELE) ? 0 : FEDIT_RDONLY))
         ) == 0) {
      mdot = getmdot(1);
      setdot(message + mdot - 1);
   }
   NYD_LEAVE;
   return val;
}

FL int
c_noop(void *v)
{
   int rv = 0;
   NYD_ENTER;
   UNUSED(v);

   switch (mb.mb_type) {
   case MB_POP3:
#ifdef HAVE_POP3
      pop3_noop();
#else
      rv = c_cmdnotsupp(NULL);
#endif
      break;
   default:
      break;
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_remove(void *v)
{
   char const *fmt;
   size_t fmt_len;
   char **args, *name, *ename;
   int ec;
   NYD_ENTER;

   if (*(args = v) == NULL) {
      n_err(_("Synopsis: remove: <mailbox>...\n"));
      ec = 1;
      goto jleave;
   }

   ec = 0;

   fmt = _("Remove %s");
   fmt_len = strlen(fmt);
   do {
      if ((name = expand(*args)) == NULL)
         continue;
      ename = n_shexp_quote_cp(name, FAL0);

      if (!strcmp(name, mailname)) {
         n_err(_("Cannot remove current mailbox %s\n"), ename);
         ec |= 1;
         continue;
      }
      {
         size_t vl = strlen(ename) + fmt_len +1;
         char *vb = salloc(vl);
         bool_t asw;
         snprintf(vb, vl, fmt, ename);
         asw = getapproval(vb, TRU1);
         if (!asw)
            continue;
      }

      switch (which_protocol(name)) {
      case PROTO_FILE:
         if (unlink(name) == -1) { /* TODO do not handle .gz .bz2 .xz.. */
            int se = errno;

            if (se == EISDIR) {
               struct stat sb;

               if (!stat(name, &sb) && S_ISDIR(sb.st_mode)) {
                  if (!rmdir(name))
                     break;
                  se = errno;
               }
            }
            n_perr(name, se);
            ec |= 1;
         }
         break;
      case PROTO_POP3:
         n_err(_("Cannot remove POP3 mailbox %s\n"), ename);
         ec |= 1;
         break;
      case PROTO_MAILDIR:
         if (maildir_remove(name) != OKAY)
            ec |= 1;
         break;
      case PROTO_UNKNOWN:
         n_err(_("Not removed: unknown protocol: %s\n"), ename);
         ec |= 1;
         break;
      }
   } while (*++args != NULL);
jleave:
   NYD_LEAVE;
   return ec;
}

FL int
c_rename(void *v)
{
   char **args = v, *old, *new;
   enum protocol oldp, newp;
   int ec;
   NYD_ENTER;

   ec = 1;

   if (args[0] == NULL || args[1] == NULL || args[2] != NULL) {
      n_err(_("Synopsis: rename: <old> <new>\n"));
      goto jleave;
   }

   if ((old = expand(args[0])) == NULL)
      goto jleave;
   oldp = which_protocol(old);
   if ((new = expand(args[1])) == NULL)
      goto jleave;
   newp = which_protocol(new);

   if (!strcmp(old, mailname) || !strcmp(new, mailname)) {
      n_err(_("Cannot rename current mailbox %s\n"),
         n_shexp_quote_cp(old, FAL0));
      goto jleave;
   }

   ec = 0;

   if (newp == PROTO_POP3)
      goto jnopop3;
   switch (oldp) {
   case PROTO_FILE:
      if (link(old, new) == -1) {
         switch (errno) {
         case EACCES:
         case EEXIST:
         case ENAMETOOLONG:
         case ENOENT:
         case ENOSPC:
         case EXDEV:
            n_perr(new, 0);
            break;
         default:
            n_perr(old, 0);
            break;
         }
         ec |= 1;
      } else if (unlink(old) == -1) {
         n_perr(old, 0);
         ec |= 1;
      }
      break;
   case PROTO_MAILDIR:
      if (rename(old, new) == -1) {
         n_perr(old, 0);
         ec |= 1;
      }
      break;
   case PROTO_POP3:
jnopop3:
      n_err(_("Cannot rename POP3 mailboxes\n"));
      ec |= 1;
      break;
   case PROTO_UNKNOWN:
   default:
      n_err(_("Unknown protocol in %s and %s; not renamed\n"),
         n_shexp_quote_cp(old, FAL0), n_shexp_quote_cp(new, FAL0));
      ec |= 1;
      break;
   }
jleave:
   NYD_LEAVE;
   return ec;
}

FL int
c_urlencode(void *v) /* XXX IDNA?? */
{
   char **ap;
   NYD_ENTER;

   for (ap = v; *ap != NULL; ++ap) {
      char *in = *ap, *out = urlxenc(in, FAL0);

      printf(" in: <%s> (%" PRIuZ " bytes)\nout: <%s> (%" PRIuZ " bytes)\n",
         in, strlen(in), out, strlen(out));
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_urldecode(void *v) /* XXX IDNA?? */
{
   char **ap;
   NYD_ENTER;

   for (ap = v; *ap != NULL; ++ap) {
      char *in = *ap, *out = urlxdec(in);

      printf(" in: <%s> (%" PRIuZ " bytes)\nout: <%s> (%" PRIuZ " bytes)\n",
         in, strlen(in), out, strlen(out));
   }
   NYD_LEAVE;
   return 0;
}

/* s-it-mode */
