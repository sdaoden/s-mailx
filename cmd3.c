/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Still more user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

struct cond_stack {
   struct cond_stack *c_outer;
   bool_t            c_noop;  /* Outer stack !c_go, entirely no-op */
   bool_t            c_go;    /* Green light */
   bool_t            c_else;  /* In `else' clause */
   ui8_t             __dummy[5];
};

static struct cond_stack   *_cond_stack;
static char                *_bang_buf;
static size_t              _bang_size;

/* Modify subject we reply to to begin with Re: if it does not already */
static char *     _reedit(char *subj);

/* Expand the shell escape by expanding unescaped !'s into the last issued
 * command where possible */
static void       _bangexp(char **str, size_t *size);

static void       make_ref_and_cs(struct message *mp, struct header *head);

/* Get PTF to implementation of command `c' (i.e., take care for *flipr*) */
static int (*     respond_or_Respond(int c))(int *, int);

/* Reply to a single message.  Extract each name from the message header and
 * send them off to mail1() */
static int        respond_internal(int *msgvec, int recipient_record);

/* Reply to a series of messages by simply mailing to the senders and not
 * messing around with the To: and Cc: lists as in normal reply */
static int        Respond_internal(int *msgvec, int recipient_record);

/* Forward a message to a new recipient, in the sense of RFC 2822 */
static int        forward1(char *str, int recipient_record);

/* Modify the subject we are replying to to begin with Fwd: */
static char *     fwdedit(char *subj);

/* Sort the passed string vecotor into ascending dictionary order */
static void       asort(char **list);

/* Do a dictionary order comparison of the arguments from qsort */
static int        diction(void const *a, void const *b);

/* Do the real work of resending */
static int        resend1(void *v, int add_resent);

/* ..to stdout */
static void       list_shortcuts(void);

/* */
static enum okay  delete_shortcut(char const *str);

static char *
_reedit(char *subj)
{
   struct str in, out;
   char *newsubj = NULL;
   NYD_ENTER;

   if (subj == NULL || *subj == '\0')
      goto j_leave;

   in.s = subj;
   in.l = strlen(subj);
   mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);

   /* TODO _reedit: should be localizable (see cmd1.c:__subject_trim()!) */
   if ((out.s[0] == 'r' || out.s[0] == 'R') &&
         (out.s[1] == 'e' || out.s[1] == 'E') && out.s[2] == ':') {
      newsubj = savestr(out.s);
      goto jleave;
   }
   newsubj = salloc(out.l + 4 +1);
   sstpcpy(sstpcpy(newsubj, "Re: "), out.s);
jleave:
   free(out.s);
j_leave:
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
      head->h_charset = mime_getparam("charset", cp);
jleave:
   NYD_LEAVE;
}

static int
(*respond_or_Respond(int c))(int *, int)
{
   int opt;
   int (*rv)(int*, int);
   NYD_ENTER;

   opt = ok_blook(Replyall);
   opt += ok_blook(flipr);
   rv = ((opt == 1) ^ (c == 'R')) ? &Respond_internal : &respond_internal;
   NYD_LEAVE;
   return rv;
}

static int
respond_internal(int *msgvec, int recipient_record)
{
   struct header head;
   struct message *mp;
   char *cp, *rcv;
   struct name *np = NULL;
   enum gfield gf;
   int rv = 1;
   NYD_ENTER;

   gf = ok_blook(fullnames) ? GFULL : GSKIN;

   if (msgvec[1] != 0) {
      fprintf(stderr, tr(37,
         "Sorry, can't reply to multiple messages at once\n"));
      goto jleave;
   }

   mp = message + msgvec[0] - 1;
   touch(mp);
   setdot(mp);

   if ((rcv = hfield1("reply-to", mp)) == NULL)
      if ((rcv = hfield1("from", mp)) == NULL)
         rcv = nameof(mp, 1);
   if (rcv != NULL)
      np = lextract(rcv, GTO | gf);
   if (!ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL)
      np = cat(np, lextract(cp, GTO | gf));
   /* Delete my name from reply list, and with it, all my alternate names */
   np = elide(delete_alternates(np));
   if (np == NULL)
      np = lextract(rcv, GTO | gf);

   memset(&head, 0, sizeof head);
   head.h_to = np;
   head.h_subject = hfield1("subject", mp);
   head.h_subject = _reedit(head.h_subject);

   /* Cc: */
   np = NULL;
   if (ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL)
      np = lextract(cp, GCC | gf);
   if ((cp = hfield1("cc", mp)) != NULL)
      np = cat(np, lextract(cp, GCC | gf));
   if (np != NULL)
      head.h_cc = elide(delete_alternates(np));
   make_ref_and_cs(mp, &head);

   if (ok_blook(quote_as_attachment)) {
      head.h_attach = csalloc(1, sizeof *head.h_attach);
      head.h_attach->a_msgno = *msgvec;
      head.h_attach->a_content_description = tr(512,
            "Original message content");
   }

   if (mail1(&head, 1, mp, NULL, recipient_record, 0) == OKAY &&
         ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static int
Respond_internal(int *msgvec, int recipient_record)
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
      mp = message + *ap - 1;
      touch(mp);
      setdot(mp);
      if ((cp = hfield1("reply-to", mp)) == NULL)
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
      head.h_attach->a_content_description = tr(512,
         "Original message content");
   }

   if (mail1(&head, 1, mp, NULL, recipient_record, 0) == OKAY &&
         ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;
jleave:
   NYD_LEAVE;
   return 0;
}

static int
forward1(char *str, int recipient_record)
{
   struct header head;
   int *msgvec, rv = 1;
   char *recipient;
   struct message *mp;
   bool_t f, forward_as_attachment;
   NYD_ENTER;

   if ((recipient = laststring(str, &f, 0)) == NULL) {
      puts(tr(47, "No recipient specified."));
      goto jleave;
   }

   forward_as_attachment = ok_blook(forward_as_attachment);
   msgvec = salloc((msgCount + 2) * sizeof *msgvec);

   if (!f) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (inhook) {
            rv = 0;
            goto jleave;
         }
         printf("No messages to forward.\n");
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;

   if (*msgvec == 0) {
      if (inhook) {
         rv = 0;
         goto jleave;
      }
      printf("No applicable messages.\n");
      goto jleave;
   }
   if (msgvec[1] != 0) {
      printf("Cannot forward multiple messages at once\n");
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
      head.h_attach->a_content_description = "Forwarded message";
   } else {
      touch(mp);
      setdot(mp);
   }
   head.h_subject = hfield1("subject", mp);
   head.h_subject = fwdedit(head.h_subject);
   mail1(&head, 1, (forward_as_attachment ? NULL : mp), NULL, recipient_record,
      1);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

static char *
fwdedit(char *subj)
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
   memcpy(newsubj + 5, out.s, out.l + 1);
   free(out.s);
jleave:
   NYD_LEAVE;
   return newsubj;
}

static void
asort(char **list)
{
   char **ap;
   size_t i;
   NYD_ENTER;

   for (ap = list; *ap != NULL; ++ap)
      ;
   if ((i = PTR2SIZE(ap - list)) >= 2)
      qsort(list, i, sizeof *list, diction);
   NYD_LEAVE;
}

static int
diction(void const *a, void const *b)
{
   int rv;
   NYD_ENTER;

   rv = strcmp(*(char**)UNCONST(a), *(char**)UNCONST(b));
   NYD_LEAVE;
   return rv;
}

static int
resend1(void *v, int add_resent)
{
   char *name, *str;
   struct name *to, *sn;
   int *ip, *msgvec;
   bool_t f = TRU1;
   NYD_ENTER;

   str = v;
   msgvec = salloc((msgCount + 2) * sizeof *msgvec);
   name = laststring(str, &f, 1);
   if (name == NULL) {
      puts(tr(47, "No recipient specified."));
      goto jleave;
   }

   if (!f) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (inhook) {
            f = FAL0;
            goto jleave;
         }
         puts(tr(48, "No applicable messages."));
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;

   if (*msgvec == 0) {
      if (inhook) {
         f = FAL0;
         goto jleave;
      }
      printf("No applicable messages.\n");
      goto jleave;
   }

   sn = nalloc(name, GTO);
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

static void
list_shortcuts(void)
{
   struct shortcut *s;
   NYD_ENTER;

   for (s = shortcuts; s != NULL; s = s->sh_next)
      printf("%s=%s\n", s->sh_short, s->sh_long);
   NYD_LEAVE;
}

static enum okay
delete_shortcut(char const *str)
{
   struct shortcut *sp, *sq;
   enum okay rv = STOP;
   NYD_ENTER;

   for (sp = shortcuts, sq = NULL; sp != NULL; sq = sp, sp = sp->sh_next) {
      if (!strcmp(sp->sh_short, str)) {
         free(sp->sh_short);
         free(sp->sh_long);
         if (sq != NULL)
            sq->sh_next = sp->sh_next;
         if (sp == shortcuts)
            shortcuts = sp->sh_next;
         free(sp);
         rv = OKAY;
         break;
      }
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_shell(void *v)
{
   char const *sh = NULL;
   char *str = v, *cmd;
   size_t cmdsize;
   sighandler_type sigint;
   NYD_ENTER;

   cmd = smalloc(cmdsize = strlen(str) +1);
   memcpy(cmd, str, cmdsize);
   _bangexp(&cmd, &cmdsize);
   if ((sh = ok_vlook(SHELL)) == NULL)
      sh = XSHELL;

   sigint = safe_signal(SIGINT, SIG_IGN);
   run_command(sh, 0, -1, -1, "-c", cmd, NULL);
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
         fprintf(stderr, tr(91, "Unknown command: `%s'\n"), arg);
#else
      ret = c_cmdnotsupp(NULL);
#endif
      goto jleave;
   }

   /* Very ugly, but take care for compiler supported string lengths :( */
   printf(tr(295, "%s commands:\n"), progname);
   puts(tr(296,
"type <message list>         type messages\n"
"next                        goto and type next message\n"
"from <message list>         give head lines of messages\n"
"headers                     print out active message headers\n"
"delete <message list>       delete messages\n"
"undelete <message list>     undelete messages\n"));
   puts(tr(297,
"save <message list> folder  append messages to folder and mark as saved\n"
"copy <message list> folder  append messages to folder without marking them\n"
"write <message list> file   append message texts to file, save attachments\n"
"preserve <message list>     keep incoming messages in mailbox even if saved\n"
"Reply <message list>        reply to message senders\n"
"reply <message list>        reply to message senders and all recipients\n"));
   puts(tr(298,
"mail addresses              mail to specific recipients\n"
"file folder                 change to another folder\n"
"quit                        quit and apply changes to folder\n"
"xit                         quit and discard changes made to folder\n"
"!                           shell escape\n"
"cd <directory>              chdir to directory or home if none given\n"
"list                        list names of all available commands\n"));
   printf(tr(299,
"\nA <message list> consists of integers, ranges of same, or other criteria\n"
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
c_respond(void *v)
{
   int rv;
   NYD_ENTER;

   rv = (*respond_or_Respond('r'))(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_respondall(void *v)
{
   int rv;
   NYD_ENTER;

   rv = respond_internal(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_respondsender(void *v)
{
   int rv;
   NYD_ENTER;

   rv = Respond_internal(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Respond(void *v)
{
   int rv;
   NYD_ENTER;

   rv = (*respond_or_Respond('R'))(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_followup(void *v)
{
   int rv;
   NYD_ENTER;

   rv = (*respond_or_Respond('r'))(v, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_followupall(void *v)
{
   int rv;
   NYD_ENTER;

   rv = respond_internal(v, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_followupsender(void *v)
{
   int rv;
   NYD_ENTER;

   rv = Respond_internal(v, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Followup(void *v)
{
   int rv;
   NYD_ENTER;

   rv = (*respond_or_Respond('R'))(v, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_forward(void *v)
{
   int rv;
   NYD_ENTER;

   rv = forward1(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_Forward(void *v)
{
   int rv;
   NYD_ENTER;

   rv = forward1(v, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_resend(void *v)
{
   int rv;
   NYD_ENTER;

   rv = resend1(v, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Resend(void *v)
{
   int rv;
   NYD_ENTER;

   rv = resend1(v, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_preserve(void *v)
{
   int *msgvec = v, *ip, mesg, rv = 1;
   struct message *mp;
   NYD_ENTER;

   if (edit) {
      printf(tr(39, "Cannot \"preserve\" in edit mode\n"));
      goto jleave;
   }

   for (ip = msgvec; *ip != 0; ++ip) {
      mesg = *ip;
      mp = message + mesg - 1;
      mp->m_flag |= MPRESERVE;
      mp->m_flag &= ~MBOX;
      setdot(mp);
      did_print_dot = TRU1;
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
      did_print_dot = TRU1;
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
      printf("/%lu\n", (ul_it)mp->m_xsize);
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_rexit(void *v)
{
   UNUSED(v);
   NYD_ENTER;

   if (!sourcing)
      exit(0);
   NYD_LEAVE;
   return 1;
}

FL int
c_set(void *v)
{
   char **ap = v, *cp, *cp2, *varbuf, c;
   int errs = 0;
   NYD_ENTER;

   if (*ap == NULL) {
      var_list_all();
      goto jleave;
   }

   for (; *ap != NULL; ++ap) {
      cp = *ap;
      cp2 = varbuf = ac_alloc(strlen(cp) +1);
      for (; (c = *cp) != '=' && c != '\0'; ++cp)
         *cp2++ = c;
      *cp2 = '\0';
      if (c == '\0')
         cp = UNCONST("");
      else
         ++cp;
      if (varbuf == cp2) {
         fprintf(stderr, tr(41, "Non-null variable name required\n"));
         ++errs;
         goto jnext;
      }
      if (varbuf[0] == 'n' && varbuf[1] == 'o')
         errs += _var_vokclear(&varbuf[2]);
      else
         errs += _var_vokset(varbuf, (uintptr_t)cp);
jnext:
      ac_free(varbuf);
   }
jleave:
   NYD_LEAVE;
   return errs;
}

FL int
c_unset(void *v)
{
   int errs;
   char **ap;
   NYD_ENTER;

   errs = 0;
   for (ap = v; *ap != NULL; ++ap)
      errs += _var_vokclear(*ap);
   NYD_LEAVE;
   return errs;
}

FL int
c_group(void *v)
{
   char **argv = v, **ap, *gname, **p;
   struct grouphead *gh;
   struct group *gp;
   int h, s;
   NYD_ENTER;

   if (*argv == NULL) {
      for (h = 0, s = 1; h < HSHSIZE; ++h)
         for (gh = groups[h]; gh != NULL; gh = gh->g_link)
            ++s;
      ap = salloc(s * sizeof *ap);

      for (h = 0, p = ap; h < HSHSIZE; ++h)
         for (gh = groups[h]; gh != NULL; gh = gh->g_link)
            *p++ = gh->g_name;
      *p = NULL;

      asort(ap);

      for (p = ap; *p != NULL; ++p)
         printgroup(*p);
      goto jleave;
   }

   if (argv[1] == NULL) {
      printgroup(*argv);
      goto jleave;
   }

   gname = *argv;
   h = hash(gname);
   if ((gh = findgroup(gname)) == NULL) {
      gh = scalloc(1, sizeof *gh);
      gh->g_name = sstrdup(gname);
      gh->g_list = NULL;
      gh->g_link = groups[h];
      groups[h] = gh;
   }

   /* Insert names from the command list into the group.  Who cares if there
    * are duplicates?  They get tossed later anyway */
   for (ap = argv + 1; *ap != NULL; ++ap) {
      gp = scalloc(1, sizeof *gp);
      gp->ge_name = sstrdup(*ap);
      gp->ge_link = gh->g_list;
      gh->g_list = gp;
   }
jleave:
   NYD_LEAVE;
   return 0;
}

FL int
c_ungroup(void *v)
{
   char **argv = v;
   int rv = 1;
   NYD_ENTER;

   if (*argv == NULL) {
      fprintf(stderr, tr(209, "Must specify alias to remove\n"));
      goto jleave;
   }

   do
      remove_group(*argv);
   while (*++argv != NULL);
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_file(void *v)
{
   char **argv = v;
   int i;
   NYD_ENTER;

   if (*argv == NULL) {
      newfileinfo();
      i = 0;
      goto jleave;
   }

   if (inhook) {
      fprintf(stderr, tr(516, "Cannot change folder from within a hook.\n"));
      i = 1;
      goto jleave;
   }

   save_mbox_for_possible_quitstuff();

   i = setfile(*argv, 0);
   if (i < 0) {
      i = 1;
      goto jleave;
   }
   callhook(mailname, 0);
   if (i > 0 && !ok_blook(emptystart)) {
      i = 1;
      goto jleave;
   }
   announce(ok_blook(bsdcompat) || ok_blook(bsdannounce));
   i = 0;
jleave:
   NYD_LEAVE;
   return i;
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
c_if(void *v)
{
   struct cond_stack *csp;
   int rv = 1;
   char **argv = v, *cp, *op;
   NYD_ENTER;

   csp = smalloc(sizeof *csp);
   csp->c_outer = _cond_stack;
   csp->c_noop = condstack_isskip();
   csp->c_go = TRU1;
   csp->c_else = FAL0;
   _cond_stack = csp;

   cp = argv[0];
   if (*cp != '$' && argv[1] != NULL) {
jesyn:
      fprintf(stderr, tr(528, "Invalid conditional expression \"%s %s %s\"\n"),
         argv[0], (argv[1] != NULL ? argv[1] : ""),
         (argv[2] != NULL ? argv[2] : ""));
      goto jleave;
   }

   switch (*cp) {
   case '0':
      csp->c_go = FAL0;
      break;
   case 'R': case 'r':
      csp->c_go = !(options & OPT_SENDMODE);
      break;
   case 'S': case 's':
      csp->c_go = ((options & OPT_SENDMODE) != 0);
      break;
   case 'T': case 't':
      csp->c_go = ((options & OPT_TTYIN) != 0);
      break;
   case '$':
      /* Look up the value in question, we need it anyway */
      v = vok_vlook(++cp);

      /* Single argument, "implicit boolean" form? */
      if ((op = argv[1]) == NULL) {
         csp->c_go = (v != NULL);
         break;
      }

      /* Three argument comparison form? */
      if (argv[2] == NULL || op[0] == '\0' || op[1] != '=' || op[2] != '\0')
         goto jesyn;
      /* A null value is treated as the empty string */
      if (v == NULL)
         v = UNCONST("");
      if (strcmp(v, argv[2]))
         v = NULL;
      switch (op[0]) {
      case '!':
      case '=':
         csp->c_go = ((op[0] == '=') ^ (v == NULL));
         break;
      default:
         goto jesyn;
      }
      break;
   default:
      fprintf(stderr, tr(43, "Unrecognized if-keyword: \"%s\"\n"), cp);
   case '1':
      csp->c_go = TRU1;
      goto jleave;
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
c_else(void *v)
{
   int rv;
   NYD_ENTER;
   UNUSED(v);

   if (_cond_stack == NULL || _cond_stack->c_else) {
      fprintf(stderr, tr(44, "\"else\" without matching \"if\"\n"));
      rv = 1;
   } else {
      _cond_stack->c_go = !_cond_stack->c_go;
      _cond_stack->c_else = TRU1;
      rv = 0;
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_endif(void *v)
{
   struct cond_stack *csp;
   int rv;
   NYD_ENTER;
   UNUSED(v);

   if ((csp = _cond_stack) == NULL) {
      fprintf(stderr, tr(46, "\"endif\" without matching \"if\"\n"));
      rv = 1;
   } else {
      _cond_stack = csp->c_outer;
      free(csp);
      rv = 0;
   }
   NYD_LEAVE;
   return rv;
}

FL bool_t
condstack_isskip(void)
{
   bool_t rv;
   NYD_ENTER;

   rv = (_cond_stack != NULL && (_cond_stack->c_noop || !_cond_stack->c_go));
   NYD_LEAVE;
   return rv;
}

FL void *
condstack_release(void)
{
   void *rv;
   NYD_ENTER;

   rv = _cond_stack;
   _cond_stack = NULL;
   NYD_LEAVE;
   return rv;
}

FL bool_t
condstack_take(void *self)
{
   struct cond_stack *csp;
   bool_t rv;
   NYD_ENTER;

   if (!(rv = ((csp = _cond_stack) == NULL)))
      do {
         _cond_stack = csp->c_outer;
         free(csp);
      } while ((csp = _cond_stack) != NULL);

   _cond_stack = self;
   NYD_LEAVE;
   return rv;
}

FL int
c_alternates(void *v)
{
   size_t l;
   char **namelist = v, **ap, **ap2, *cp;
   NYD_ENTER;

   l = argcount(namelist) + 1;
   if (l == 1) {
      if (altnames == NULL)
         goto jleave;
      for (ap = altnames; *ap != NULL; ++ap)
         printf("%s ", *ap);
      printf("\n");
      goto jleave;
   }

   if (altnames != NULL) {
      for (ap = altnames; *ap != NULL; ++ap)
         free(*ap);
      free(altnames);
   }
   altnames = smalloc(l * sizeof(char*));
   for (ap = namelist, ap2 = altnames; *ap != NULL; ++ap, ++ap2) {
      l = strlen(*ap) + 1;
      cp = smalloc(l);
      memcpy(cp, *ap, l);
      *ap2 = cp;
   }
   *ap2 = NULL;
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
         (val = setfile(mailname, 1)) == 0) {
      mdot = getmdot(1);
      setdot(message + mdot - 1);
   }
   NYD_LEAVE;
   return val;
}

FL int
c_shortcut(void *v)
{
   char **args = v;
   struct shortcut *s;
   int rv;
   NYD_ENTER;

   if (args[0] == NULL) {
      list_shortcuts();
      rv = 0;
      goto jleave;
   }

   rv = 1;
   if (args[1] == NULL) {
      fprintf(stderr, tr(220, "expansion name for shortcut missing\n"));
      goto jleave;
   }
   if (args[2] != NULL) {
      fprintf(stderr, tr(221, "too many arguments\n"));
      goto jleave;
   }

   if ((s = get_shortcut(args[0])) != NULL) {
      free(s->sh_long);
      s->sh_long = sstrdup(args[1]);
   } else {
      s = scalloc(1, sizeof *s);
      s->sh_short = sstrdup(args[0]);
      s->sh_long = sstrdup(args[1]);
      s->sh_next = shortcuts;
      shortcuts = s;
   }
   rv = 0;
jleave:
   NYD_LEAVE;
   return rv;
}

FL struct shortcut *
get_shortcut(char const *str)
{
   struct shortcut *s;
   NYD_ENTER;

   for (s = shortcuts; s != NULL; s = s->sh_next)
      if (!strcmp(str, s->sh_short))
         break;
   NYD_LEAVE;
   return s;
}

FL int
c_unshortcut(void *v)
{
   char **args = v;
   bool_t errs = FAL0;
   NYD_ENTER;

   if (args[0] == NULL) {
      fprintf(stderr, tr(222, "need shortcut names to remove\n"));
      errs = TRU1;
      goto jleave;
   }

   while (*args != NULL) {
      if (delete_shortcut(*args) != OKAY) {
         errs = TRU1;
         fprintf(stderr, tr(223, "%s: no such shortcut\n"), *args);
      }
      ++args;
   }
jleave:
   NYD_LEAVE;
   return errs;
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
      fprintf(stderr, tr(290, "Syntax is: remove mailbox ...\n"));
      ec = 1;
      goto jleave;
   }

   fmt = tr(287, "Remove \"%s\" (y/n) ? ");
   fmt_len = strlen(fmt);
   do {
      if ((name = expand(*args)) == NULL)
         continue;

      if (!strcmp(name, mailname)) {
         fprintf(stderr, tr(286, "Cannot remove current mailbox \"%s\".\n"),
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
         fprintf(stderr, tr(288, "Cannot remove POP3 mailbox \"%s\".\n"),name);
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
         fprintf(stderr, tr(289, "Unknown protocol in \"%s\". Not removed.\n"),
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
      fprintf(stderr, tr(291, "Cannot rename current mailbox \"%s\".\n"), old);
      goto jleave;
   }
   if ((oldp == PROTO_IMAP || newp == PROTO_IMAP) && oldp != newp) {
      fprintf(stderr, tr(292, "Can only rename folders of same type.\n"));
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
      fprintf(stderr, tr(293, "Cannot rename POP3 mailboxes.\n"));
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
      fprintf(stderr, tr(294,
         "Unknown protocol in \"%s\" and \"%s\".  Not renamed.\n"), old, new);
      ec |= 1;
      break;
   }
jleave:
   NYD_LEAVE;
   return ec;
}

/* vim:set fenc=utf-8:s-it-mode */
