/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Still more user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
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
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
make_ref_and_cs(struct message *mp, struct header *head)
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

   newref = ac_alloc(reflen);
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
   ac_free(newref);

   /* Limit number of references */
   while (n->n_flink != NULL)
      n = n->n_flink;
   for (i = 1; i < 21; ++i) { /* XXX no magics */
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
   int rv = 1;
   NYD_ENTER;

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
      char const *tr = _("Reply-To \"%s%s\"");
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
      char const *tr = _("Followup-To \"%s%s\"");
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
         struct name *x = lextract(cp, GEXTRA | GSKIN);
         if (x == NULL || x->n_flink != NULL ||
               !is_prefix("mailto:", x->n_name) ||
               /* XXX the mailto: prefix causes failure (":" invalid character)
                * XXX which is why need to recreate a struct name with an
                * XXX updated name; this is terribly wasteful and can't we find
                * XXX a way to mitigate that?? */
               is_addr_invalid(x = nalloc(x->n_name + sizeof("mailto:") -1,
                  GEXTRA | GSKIN), EACM_STRICT)) {
            if (options & OPT_D_V)
               fprintf(stderr,
                  _("Message contains invalid List-Post: header\n"));
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
      sleep(1);
      goto jnext_msg;
   }
   rv = 0;
   NYD_LEAVE;
   return rv;
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
         char const *tr = _("Reply-To \"%s%s\"");
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
         if (pstate & PS_IN_HOOK) {
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
      if (pstate & PS_IN_HOOK) {
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
         if (pstate & PS_IN_HOOK) {
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
      if (pstate & PS_IN_HOOK) {
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

   if (pstate & PS_IN_HOOK) {
      fprintf(stderr, _("Cannot change folder from within a hook.\n"));
      i = 1;
      goto jleave;
   }

   save_mbox_for_possible_quitstuff();

   i = setfile(*argv, fm);
   if (i < 0) {
      i = 1;
      goto jleave;
   }
   assert(!(fm & FEDIT_NEWMAIL));
   check_folder_hook(FAL0);

   if (i > 0 && !ok_blook(emptystart)) {
      i = 1;
      goto jleave;
   }
   announce(ok_blook(bsdcompat) || ok_blook(bsdannounce));
   i = 0;
jleave:
   NYD2_LEAVE;
   return i;
}

FL int
c_shell(void *v)
{
   char const *sh = NULL;
   char *str = v, *cmd;
   size_t cmdsize;
   sigset_t mask;
   sighandler_type sigint;
   NYD_ENTER;

   cmd = smalloc(cmdsize = strlen(str) +1);
   memcpy(cmd, str, cmdsize);
   _bangexp(&cmd, &cmdsize);
   if ((sh = ok_vlook(SHELL)) == NULL)
      sh = XSHELL;

   sigint = safe_signal(SIGINT, SIG_IGN);
   sigemptyset(&mask);
   run_command(sh, &mask, -1, -1, "-c", cmd, NULL);
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
   char const *sh;
   NYD_ENTER;
   UNUSED(v);

   if ((sh = ok_vlook(SHELL)) == NULL)
      sh = XSHELL;

   sigint = safe_signal(SIGINT, SIG_IGN);
   run_command(sh, 0, -1, -1, NULL, NULL, NULL);
   safe_signal(SIGINT, sigint);
   putchar('\n');
   NYD_LEAVE;
   return 0;
}

FL int
c_help(void *v)
{
   int ret = 0;
   char *arg;
   NYD_ENTER;

   arg = *(char**)v;

   if (arg != NULL) {
#ifdef HAVE_DOCSTRINGS
      ret = !print_comm_docstr(arg);
      if (ret)
         fprintf(stderr, _("Unknown command: `%s'\n"), arg);
#else
      ret = c_cmdnotsupp(NULL);
#endif
      goto jleave;
   }

   /* Very ugly, but take care for compiler supported string lengths :( */
   printf(_("%s commands:\n"), progname);
   puts(_(
"type <message list>         type messages\n"
"next                        goto and type next message\n"
"from <message list>         give head lines of messages\n"
"headers                     print out active message headers\n"
"delete <message list>       delete messages\n"
"undelete <message list>     undelete messages\n"));
   puts(_(
"save <message list> folder  append messages to folder and mark as saved\n"
"copy <message list> folder  append messages to folder without marking them\n"
"write <message list> file   append message texts to file, save attachments\n"
"preserve <message list>     keep incoming messages in mailbox even if saved\n"
"Reply <message list>        reply to message senders\n"
"reply <message list>        reply to message senders and all recipients\n"));
   puts(_(
"mail addresses              mail to specific recipients\n"
"file folder                 change to another folder\n"
"quit                        quit and apply changes to folder\n"
"xit                         quit and discard changes made to folder\n"
"!                           shell escape\n"
"cd <directory>              chdir to directory or home if none given\n"
"list                        list names of all available commands\n"));
   printf(_(
"A <message list> consists of integers, ranges of same, or other criteria\n"
"separated by spaces.  If omitted, %s uses the last message typed.\n"),
      progname);

jleave:
   NYD_LEAVE;
   return ret;
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
      perror("getcwd");
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
      cp = homedir;
   else if ((cp = file_expand(*arglist)) == NULL)
      goto jleave;
   if (chdir(cp) == -1) {
      perror(cp);
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
c_preserve(void *v)
{
   int *msgvec = v, *ip, mesg, rv = 1;
   struct message *mp;
   NYD_ENTER;

   if (pstate & PS_EDIT) {
      printf(_("Cannot \"preserve\" in a system mailbox\n"));
      goto jleave;
   }

   for (ip = msgvec; *ip != 0; ++ip) {
      mesg = *ip;
      mp = message + mesg - 1;
      mp->m_flag |= MPRESERVE;
      mp->m_flag &= ~MBOX;
      setdot(mp);
      pstate |= PS_DID_PRINT_DOT;
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_unread(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      setdot(message + *ip - 1);
      dot->m_flag &= ~(MREAD | MTOUCH);
      dot->m_flag |= MSTATUS;
#ifdef HAVE_IMAP
      if (mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE)
         imap_unread(message + *ip - 1, *ip); /* TODO return? */
#endif
      pstate |= PS_DID_PRINT_DOT;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_seen(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      struct message *mp = message + *ip - 1;
      setdot(mp);
      touch(mp);
   }
   NYD_LEAVE;
   return 0;
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
c_echo(void *v)
{
   char const **argv = v, **ap, *cp;
   int c;
   NYD_ENTER;

   for (ap = argv; *ap != NULL; ++ap) {
      cp = *ap;
      if ((cp = fexpand(cp, FEXP_NSHORTCUT)) != NULL) {
         if (ap != argv)
            putchar(' ');
         c = 0;
         while (*cp != '\0' && (c = expand_shell_escape(&cp, FAL0)) > 0)
            putchar(c);
         /* \c ends overall processing */
         if (c < 0)
            goto jleave;
      }
   }
   putchar('\n');
jleave:
   NYD_LEAVE;
   return 0;
}

FL int
c_newmail(void *v)
{
   int val = 1, mdot;
   NYD_ENTER;
   UNUSED(v);

   if (
#ifdef HAVE_IMAP
         (mb.mb_type != MB_IMAP || imap_newmail(1)) &&
#endif
         (val = setfile(mailname,
            FEDIT_NEWMAIL | ((mb.mb_perm & MB_DELE) ? 0 : FEDIT_RDONLY))
         ) == 0) {
      mdot = getmdot(1);
      setdot(message + mdot - 1);
   }
   NYD_LEAVE;
   return val;
}

FL int
c_flag(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MFLAG | MFLAGGED)))
         m->m_flag |= MFLAG | MFLAGGED;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_unflag(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MFLAG | MFLAGGED)) {
         m->m_flag &= ~(MFLAG | MFLAGGED);
         m->m_flag |= MUNFLAG;
      }
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_answered(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MANSWER | MANSWERED)))
         m->m_flag |= MANSWER | MANSWERED;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_unanswered(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MANSWER | MANSWERED)) {
         m->m_flag &= ~(MANSWER | MANSWERED);
         m->m_flag |= MUNANSWER;
      }
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_draft(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MDRAFT | MDRAFTED)))
         m->m_flag |= MDRAFT | MDRAFTED;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_undraft(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MDRAFT | MDRAFTED)) {
         m->m_flag &= ~(MDRAFT | MDRAFTED);
         m->m_flag |= MUNDRAFT;
      }
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_noop(void *v)
{
   int rv = 0;
   NYD_ENTER;
   UNUSED(v);

   switch (mb.mb_type) {
   case MB_IMAP:
#ifdef HAVE_IMAP
      imap_noop();
#else
      rv = c_cmdnotsupp(NULL);
#endif
      break;
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
   char **args = v, *name;
   int ec = 0;
   NYD_ENTER;

   if (*args == NULL) {
      fprintf(stderr, _("Syntax is: remove mailbox ...\n"));
      ec = 1;
      goto jleave;
   }

   fmt = _("Remove \"%s\" (y/n) ? ");
   fmt_len = strlen(fmt);
   do {
      if ((name = expand(*args)) == NULL)
         continue;

      if (!strcmp(name, mailname)) {
         fprintf(stderr, _("Cannot remove current mailbox \"%s\".\n"),
            name);
         ec |= 1;
         continue;
      }
      {
         size_t vl = strlen(name) + fmt_len +1;
         char *vb = ac_alloc(vl);
         bool_t asw;
         snprintf(vb, vl, fmt, name);
         asw = getapproval(vb, TRU1);
         ac_free(vb);
         if (!asw)
            continue;
      }

      switch (which_protocol(name)) {
      case PROTO_FILE:
         if (unlink(name) == -1) { /* TODO do not handle .gz .bz2 .xz.. */
            perror(name);
            ec |= 1;
         }
         break;
      case PROTO_POP3:
         fprintf(stderr, _("Cannot remove POP3 mailbox \"%s\".\n"),name);
         ec |= 1;
         break;
      case PROTO_IMAP:
#ifdef HAVE_IMAP
         if (imap_remove(name) != OKAY)
#endif
            ec |= 1;
         break;
      case PROTO_MAILDIR:
         if (maildir_remove(name) != OKAY)
            ec |= 1;
         break;
      case PROTO_UNKNOWN:
         fprintf(stderr, _("Unknown protocol in \"%s\". Not removed.\n"),
            name);
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
      fprintf(stderr, "Syntax: rename old new\n");
      goto jleave;
   }

   if ((old = expand(args[0])) == NULL)
      goto jleave;
   oldp = which_protocol(old);
   if ((new = expand(args[1])) == NULL)
      goto jleave;
   newp = which_protocol(new);

   if (!strcmp(old, mailname) || !strcmp(new, mailname)) {
      fprintf(stderr, _("Cannot rename current mailbox \"%s\".\n"), old);
      goto jleave;
   }
   if ((oldp == PROTO_IMAP || newp == PROTO_IMAP) && oldp != newp) {
      fprintf(stderr, _("Can only rename folders of same type.\n"));
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
            perror(new);
            break;
         default:
            perror(old);
         }
         ec |= 1;
      } else if (unlink(old) == -1) {
         perror(old);
         ec |= 1;
      }
      break;
   case PROTO_MAILDIR:
      if (rename(old, new) == -1) {
         perror(old);
         ec |= 1;
      }
      break;
   case PROTO_POP3:
jnopop3:
      fprintf(stderr, _("Cannot rename POP3 mailboxes.\n"));
      ec |= 1;
      break;
#ifdef HAVE_IMAP
   case PROTO_IMAP:
      if (imap_rename(old, new) != OKAY)
         ec |= 1;
      break;
#endif
   case PROTO_UNKNOWN:
   default:
      fprintf(stderr, _(
         "Unknown protocol in \"%s\" and \"%s\".  Not renamed.\n"), old, new);
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
