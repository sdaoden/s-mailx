/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ All sorts of `reply', `resend', `forward', and similar user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define n_FILE cmd_resend

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* Modify subject we reply to to begin with Re: if it does not already */
static char *a_crese_reedit(char const *subj);

static void       make_ref_and_cs(struct message *mp, struct header *head);

/* `reply' and `Lreply' workhorse */
static int a_crese_list_reply(int *msgvec, enum header_flags hf);

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

static char *
a_crese_reedit(char const *subj){
   char *newsubj;
   NYD_ENTER;

   newsubj = NULL;

   if(subj != NULL && *subj != '\0'){
      struct str in, out;
      size_t i;
      char const *cp;

      in.l = strlen(in.s = n_UNCONST(subj));
      mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);

      i = strlen(cp = subject_re_trim(out.s)) +1;
      /* RFC mandates english "Re: " */
      newsubj = salloc(sizeof("Re: ") -1 + i);
      memcpy(newsubj, "Re: ", sizeof("Re: ") -1);
      memcpy(&newsubj[sizeof("Re: ") -1], cp, i);

      free(out.s);
   }
   NYD_LEAVE;
   return newsubj;
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
         (cp = hfield1("content-type", mp)) != NULL){
      if((head->h_charset = cp = mime_param_get("charset", cp)) != NULL){
         char *cpo, c;

         for(cpo = cp; (c = *cpo) != '\0'; ++cpo)
            *cpo = lowerconv(c);

         head->h_charset = n_charsetalias_expand(cp);
      }
   }
jleave:
   NYD_LEAVE;
}

static int
a_crese_list_reply(int *msgvec, enum header_flags hf)
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

   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;

jnext_msg:
   n_autorec_relax_create();
   mp = &message[*msgvec - 1];
   touch(mp);
   setdot(mp);

   memset(&head, 0, sizeof head);
   head.h_flags = hf;
   head.h_subject = a_crese_reedit(hfield1("subject", mp));
   head.h_mailx_command = "reply";
   head.h_mailx_orig_from = lextract(hfield1("from", mp), GIDENT | gf);
   head.h_mailx_orig_to = lextract(hfield1("to", mp), GTO | gf);
   head.h_mailx_orig_cc = lextract(hfield1("cc", mp), GCC | gf);
   head.h_mailx_orig_bcc = lextract(hfield1("bcc", mp), GBCC | gf);
   rt = mft = NULL;

   rcv = NULL;
   if ((reply_to = hfield1("reply-to", mp)) != NULL &&
         (cp = ok_vlook(reply_to_honour)) != NULL &&
         (rt = checkaddrs(lextract(reply_to, GTO | gf), EACM_STRICT, NULL)
         ) != NULL) {
      char *sp;
      size_t l;
      char const *tr;

      tr = _("Reply-To %s%s");
      l = strlen(tr) + strlen(rt->n_name) + 3 +1;
      sp = n_autorec_alloc(l);
      snprintf(sp, l, tr, rt->n_name, (rt->n_flink != NULL ? "..." : n_empty));

      if(quadify(cp, UIZ_MAX, sp, TRU1) > FAL0)
         rcv = reply_to;
   }

   if (rcv == NULL && (rcv = hfield1("from", mp)) == NULL)
      rcv = nameof(mp, 1);

   /* Cc: */
   np = NULL;
   if (ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL)
      np = lextract(cp, GCC | gf);
   if ((cp = hfield1("cc", mp)) != NULL) {
      struct name *x;

      if((x = lextract(cp, GCC | gf)) != NULL)
         np = cat(np, x);
   }
   if (np != NULL) {
      head.h_mailx_raw_cc = namelist_dup(np, GCC | gf);
      head.h_cc = n_alternates_remove(np, FAL0);
   }

   /* To: */
   np = NULL;
   if (rcv != NULL)
      np = (rcv == reply_to) ? namelist_dup(rt, GTO | gf)
            : lextract(rcv, GTO | gf);
   if (!ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL) {
      struct name *x;

      if((x = lextract(cp, GTO | gf)) != NULL)
         np = cat(np, x);
   }
   /* Delete my name from reply list, and with it, all my alternate names */
   if(np != NULL){
      head.h_mailx_raw_to = namelist_dup(np, GTO | gf);
      np = n_alternates_remove(np, FAL0);
      if(count(np) == 0){
         np = lextract(rcv, GTO | gf);
         head.h_mailx_raw_to = namelist_dup(np, GTO | gf);
      }
   }
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
      char *sp;
      size_t l;
      char const *tr;

      tr = _("Followup-To %s%s");
      l = strlen(tr) + strlen(np->n_name) + 3 +1;
      sp = n_autorec_alloc(l);
      snprintf(sp, l, tr, np->n_name, (np->n_flink != NULL ? "..." : n_empty));

      if(quadify(ok_vlook(followup_to_honour), UIZ_MAX, sp, TRU1) > FAL0){
         head.h_cc = NULL;
         head.h_to = np;
         head.h_mft =
         mft = namelist_vaporise_head(&head, EACM_STRICT, FAL0, NULL);
      }else
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
            if(n_poption & n_PO_D_V)
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
         if (rt != NULL && rt->n_flink == NULL &&
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

   if (mail1(&head, 1, mp, NULL, !!(hf & HF_RECIPIENT_RECORD), 0) != OKAY) {
      msgvec = NULL;
      goto jleave;
   }
   if(ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;
   n_autorec_relax_gut();

   if (*++msgvec != 0) {
      /* TODO message (error) ring.., less sleep */
      if(n_psonce & n_PSO_INTERACTIVE){
         fprintf(n_stdout,
            _("Waiting a second before proceeding to the next message..\n"));
         fflush(n_stdout);
         n_msleep(1000, FAL0);
      }
      goto jnext_msg;
   }

jleave:
   ac_free(save_msgvec);
   NYD_LEAVE;
   return (msgvec == NULL);
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

   rv = a_crese_list_reply(msgvec,
         (recipient_record ? HF_RECIPIENT_RECORD : HF_NONE));
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
   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;

   for (ap = msgvec; *ap != 0; ++ap) {
      char const *rp;
      struct name *rt;

      mp = &message[*ap - 1];
      touch(mp);
      setdot(mp);

      if ((rp = hfield1("reply-to", mp)) != NULL &&
            (cp = ok_vlook(reply_to_honour)) != NULL &&
            (rt = checkaddrs(lextract(rp, GTO | gf), EACM_STRICT, NULL)
            ) != NULL) {
         char *sp;
         size_t l;
         char const *tr;

         tr = _("Reply-To %s%s");
         l = strlen(tr) + strlen(rt->n_name) + 3 +1;
         sp = n_autorec_alloc(l);
         snprintf(sp, l, tr, rt->n_name, (rt->n_flink != NULL ? "..."
            : n_empty));

         if(quadify(cp, UIZ_MAX, sp, TRU1) > FAL0){
            head.h_to = cat(head.h_to, rt);
            continue;
         }
      }

      if ((cp = hfield1("from", mp)) == NULL)
         cp = nameof(mp, 2);
      head.h_to = cat(head.h_to, lextract(cp, GTO | gf));
   }
   if (head.h_to == NULL) /* XXX no recipients? */
      goto jleave;
   head.h_mailx_raw_to = namelist_dup(head.h_to, GTO | gf);

   mp = &message[msgvec[0] - 1];
   head.h_subject = hfield1("subject", mp);
   head.h_subject = a_crese_reedit(head.h_subject);
   make_ref_and_cs(mp, &head);
   head.h_mailx_command = "reply";
   head.h_mailx_orig_from = lextract(hfield1("from", mp), GIDENT | gf);
   head.h_mailx_orig_to = lextract(hfield1("to", mp), GTO | gf);
   head.h_mailx_orig_cc = lextract(hfield1("cc", mp), GCC | gf);
   head.h_mailx_orig_bcc = lextract(hfield1("bcc", mp), GBCC | gf);

   if (ok_blook(quote_as_attachment)) {
      head.h_attach = csalloc(1, sizeof *head.h_attach);
      head.h_attach->a_msgno = *msgvec;
      head.h_attach->a_content_description = _("Original message content");
   }

   if (mail1(&head, 1, mp, NULL, recipient_record, 0) != OKAY) {
      msgvec = NULL;
      goto jleave;
   }

   if(ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;
jleave:
   NYD_LEAVE;
   return (msgvec == NULL);
}

static int
_fwd(char *str, int recipient_record)
{
   struct header head;
   int *msgvec, rv = 1;
   struct message *mp;
   enum gfield gf;
   bool_t f, forward_as_attachment;
   char *recipient;
   NYD_ENTER;

   if ((recipient = laststring(str, &f, TRU1)) == NULL) {
      fputs(_("No recipient specified.\n"), n_stdout);
      goto jleave;
   }

   forward_as_attachment = ok_blook(forward_as_attachment);
   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;
   msgvec = salloc((msgCount + 2) * sizeof *msgvec);

   if (!f) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) {
            rv = 0;
            goto jleave;
         }
         fprintf(n_stdout, _("No messages to forward.\n"));
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;

   if (*msgvec == 0) {
      if (n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) {
         rv = 0;
         goto jleave;
      }
      fprintf(n_stdout, _("No applicable messages.\n"));
      goto jleave;
   }
   if (msgvec[1] != 0) {
      fprintf(n_stdout, _("Cannot forward multiple messages at once\n"));
      goto jleave;
   }

   memset(&head, 0, sizeof head);
   if ((head.h_to = lextract(recipient,
         (GTO | (ok_blook(fullnames) ? GFULL : GSKIN)))) == NULL)
      goto jleave;

   mp = &message[*msgvec - 1];
   touch(mp);
   setdot(mp);
   head.h_subject = hfield1("subject", mp);
   head.h_subject = __fwdedit(head.h_subject);
   head.h_mailx_command = "forward";
   head.h_mailx_raw_to = namelist_dup(head.h_to, GTO | gf);
   head.h_mailx_orig_from = lextract(hfield1("from", mp), GIDENT | gf);
   head.h_mailx_orig_to = lextract(hfield1("to", mp), GTO | gf);
   head.h_mailx_orig_cc = lextract(hfield1("cc", mp), GCC | gf);
   head.h_mailx_orig_bcc = lextract(hfield1("bcc", mp), GBCC | gf);

   if (forward_as_attachment) {
      head.h_attach = csalloc(1, sizeof *head.h_attach);
      head.h_attach->a_msgno = *msgvec;
      head.h_attach->a_content_description = _("Forwarded message");
   }
   rv = (mail1(&head, 1, (forward_as_attachment ? NULL : mp), NULL,
         recipient_record, 1) != OKAY); /* reverse! */
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
   struct header head;
   struct name *myto, *myrawto;
   enum gfield gf;
   char *name, *str;
   int *ip, *msgvec;
   bool_t f = TRU1;
   NYD_ENTER;

   str = v;
   msgvec = salloc((msgCount + 2) * sizeof *msgvec);
   name = laststring(str, &f, TRU1);
   if (name == NULL) {
      fputs(_("No recipient specified.\n"), n_stdout);
      goto jleave;
   }

   if (!f) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) {
            f = FAL0;
            goto jleave;
         }
         fputs(_("No applicable messages.\n"), n_stdout);
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;

   if (*msgvec == 0) {
      if (n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) {
         f = FAL0;
         goto jleave;
      }
      fprintf(n_stdout, "No applicable messages.\n");
      goto jleave;
   }

   f = TRU1;
   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;

   myrawto = nalloc(name, GTO | gf);
   myto = usermap(namelist_dup(myrawto, myrawto->n_type), FAL0);
   myto = n_alternates_remove(myto, TRU1);
   if(myto == NULL){
      n_err(_("No recipients specified\n"));
      goto jleave;
   }

   n_autorec_relax_create();
   for (ip = msgvec; *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount);
         ++ip){
      struct message *mp;

      mp = &message[*ip - 1];
      touch(mp);
      setdot(mp);

      memset(&head, 0, sizeof head);
      head.h_to = myto;
      head.h_mailx_command = "resend";
      head.h_mailx_raw_to = myrawto;
      head.h_mailx_orig_from = lextract(hfield1("from", mp), GIDENT | gf);
      head.h_mailx_orig_to = lextract(hfield1("to", mp), GTO | gf);
      head.h_mailx_orig_cc = lextract(hfield1("cc", mp), GCC | gf);
      head.h_mailx_orig_bcc = lextract(hfield1("bcc", mp), GBCC | gf);

      if(resend_msg(mp, &head, add_resent) != OKAY){
         /* n_autorec_relax_gut(); XXX but is handled automatically? */
         goto jleave;
      }
      n_autorec_relax_unroll();
   }
   n_autorec_relax_gut();
   f = FAL0;
jleave:
   NYD_LEAVE;
   return (f != FAL0);
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

   rv = a_crese_list_reply(v, HF_LIST_REPLY);
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

/* s-it-mode */
