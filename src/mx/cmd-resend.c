/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ All sorts of `reply', `resend', `forward', and similar user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause
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
#undef su_FILE
#define su_FILE cmd_resend
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>

#include "mx/attachments.h"
#include "mx/cmd.h"
#include "mx/cmd-charsetalias.h"
#include "mx/cmd-mlist.h"
#include "mx/compat.h"
#include "mx/mime-param.h"
#include "mx/names.h"
#include "mx/url.h"
#include "mx/tty.h"

/* TODO fake */
#include "su/code-in.h"

/* Modify subject to begin with Re:/Fwd: if it does not already */
static char *a_crese_sub_edit(struct message *mp, boole isfwd);

/* Fetch these headers, as appropriate; *the_rt will be set to Reply-To:
 * regardless of whether Reply-To: will be honoured or not */
static struct mx_name *a_crese_reply_to(struct message *mp,
      struct mx_name **the_rt);
static struct mx_name *a_crese_mail_followup_to(struct message *mp);

/* We honoured Reply-To: and/or Mail-Followup-To:, but *recipients-in-cc* is
 * set so try to keep "secondary" addressees in Cc:, if possible, */
static void a_crese_polite_rt_mft_move(struct message *mp, struct header *hp,
      struct mx_name *np);

/* *reply-to-swap-in* */
static boole a_crese_do_rt_swap_in(struct header *hp, struct mx_name *the_rt);
static void a_crese_rt_swap_in(struct header *hp, struct mx_name *the_rt);

/* References and charset, as appropriate */
static void a_crese_make_ref_and_cs(struct message *mp, struct header *head);

/* `reply' and `Lreply' workhorse */
static int a_crese_list_reply(int *msgvec, enum header_flags hf);

/* Get PTF to implementation of command c (i.e., take care for *flipr*) */
static int (*a_crese_reply_or_Reply(char c))(int *, boole);

/* Reply to a single message.  Extract each name from the message header and
 * send them off to mail1() */
static int a_crese_reply(int *msgvec, boole recipient_record);

/* Reply to a series of messages by simply mailing to the senders and not
 * messing around with the To: and Cc: lists as in normal reply */
static int a_crese_Reply(int *msgvec, boole recipient_record);

/* Forward a message to a new recipient, in the sense of RFC 2822 */
static int a_crese_fwd(void *vp, boole recipient_record);

/* Do the real work of resending */
static int a_crese_resend1(void *v, boole add_resent);

static char *
a_crese_sub_edit(struct message *mp, boole isfwd){
   char *newsubj;
   BITENUM_IS(u32,mx_header_subject_edit_flags) hsef;
   char const *subj;
   NYD2_IN;

   subj = hfield1("subject", mp);
   if(subj == NIL)
      subj = su_empty;
   if((n_psonce & n_PSO_INTERACTIVE) && *subj == '\0')
      subj = _("(no subject)");

   if(isfwd)
      hsef = mx_HEADER_SUBJECT_EDIT_DECODE_MIME |
            mx_HEADER_SUBJECT_EDIT_TRIM_FWD |
            mx_HEADER_SUBJECT_EDIT_PREPEND_FWD;
   else
      hsef = mx_HEADER_SUBJECT_EDIT_DECODE_MIME |
            mx_HEADER_SUBJECT_EDIT_TRIM_RE |
            mx_HEADER_SUBJECT_EDIT_PREPEND_RE;

   if(*(newsubj = mx_header_subject_edit(subj, hsef)) == '\0')
      newsubj = NIL;

   NYD2_OU;
   return newsubj;
}

static struct mx_name *
a_crese_reply_to(struct message *mp, struct mx_name **the_rt){
   char const *cp;
   struct mx_name *rt, *np;
   enum gfield gf;
   NYD2_IN;

   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;
   rt = NIL;

   if((cp = hfield1("reply-to", mp)) != NIL)
      rt = checkaddrs(lextract(cp, GTO | gf), EACM_STRICT, NIL);

   *the_rt = rt;

   if((cp = ok_vlook(reply_to_honour)) != NIL && rt != NIL){
      char *lp;
      uz l;
      char const *tr;

      if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)){
         fprintf(n_stdout, _("Reply-To: header contains:"));
         for(np = rt; np != NIL; np = np->n_flink)
            fprintf(n_stdout, " %s", np->n_name);
         putc('\n', n_stdout);
      }

      tr = _("Reply-To %s%s");
      l = su_cs_len(tr) + su_cs_len(rt->n_name) + 3 +1;
      lp = n_lofi_alloc(l);

      snprintf(lp, l, tr, rt->n_name, (rt->n_flink != NIL ? "..." : su_empty));
      if(n_quadify(cp, UZ_MAX, lp, TRU1) <= FAL0)
         rt = NIL;

      n_lofi_free(lp);
   }else
      rt = NIL;

   NYD2_OU;
   return rt;
}

static struct mx_name *
a_crese_mail_followup_to(struct message *mp){
   char const *cp, *cp2;
   struct mx_name *mft, *np;
   enum gfield gf;
   NYD2_IN;

   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;
   mft = NULL;

   if((cp = ok_vlook(followup_to_honour)) != NULL &&
         (cp2 = hfield1("mail-followup-to", mp)) != NULL &&
         (mft = checkaddrs(lextract(cp2, GTO | gf), EACM_STRICT, NULL)
            ) != NULL){
      char *lp;
      uz l;
      char const *tr;

      if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)){
         fprintf(n_stdout, _("Mail-Followup-To: header contains:"));
         for(np = mft; np != NULL; np = np->n_flink)
            fprintf(n_stdout, " %s", np->n_name);
         putc('\n', n_stdout);
      }

      tr = _("Followup-To %s%s");
      l = su_cs_len(tr) + su_cs_len(mft->n_name) + 3 +1;
      lp = n_lofi_alloc(l);

      snprintf(lp, l, tr, mft->n_name,
         (mft->n_flink != NULL ? "..." : n_empty));
      if(n_quadify(cp, UZ_MAX, lp, TRU1) <= FAL0)
         mft = NULL;

      n_lofi_free(lp);
   }
   NYD2_OU;
   return mft;
}

static void
a_crese_polite_rt_mft_move(struct message *mp, struct header *hp,
      struct mx_name *np){
   enum{
      a_NONE,
      a_ONCE = 1u<<0,
      a_LIST_CLASSIFIED = 1u<<1,
      a_SEEN_TO = 1u<<2,
      a_ORIG_SEARCHED = 1u<<3,
      a_ORIG_FOUND = 1u<<4
   };

   struct mx_name *np_orig;
   u32 f;
   NYD2_IN;
   UNUSED(mp);

   if(np == hp->h_to)
      hp->h_to = NIL;
   if(np == hp->h_cc)
      hp->h_cc = NIL;

   /* We may find that in the end To: is empty but Cc: is not, in which case we
    * upgrade Cc: to To: and jump back and redo the thing slightly different */
   f = a_NONE;
   np_orig = np;
jredo:
   while(np != NIL){
      enum gfield gf;
      struct mx_name *nnp, **xpp, *xp;

      nnp = np;
      np = np->n_flink;

      if(f & a_ONCE){
         gf = GTO;
         xpp = &hp->h_to;
      }else{
         gf = GCC;
         xpp = &hp->h_cc;
      }

      /* Try primary, then secondary */
      for(xp = hp->h_mailx_orig_to; xp != NIL; xp = xp->n_flink)
         if(mx_name_is_same_address(xp, nnp)){
            if(!(f & a_LIST_CLASSIFIED)){
               f |= a_SEEN_TO;
               goto jclass_ok;
            }
            goto jlink;
         }

      if(f & a_ONCE){
         gf = GCC;
         xpp = &hp->h_cc;
      }

      for(xp = hp->h_mailx_orig_cc; xp != NIL; xp = xp->n_flink)
         if(mx_name_is_same_address(xp, nnp))
            goto jlink;

      /* If this receiver came in only via R-T: or M-F-T:, place her/him/it in
       * To: due to lack of a better place.  But only if To: is not empty after
       * all formerly present receivers have been worked, to avoid that yet
       * unaddressed receivers propagate to To: whereas formerly addressed ones
       * end in Cc: .. */
      if(f & a_LIST_CLASSIFIED){
         if(f & a_SEEN_TO){
            /* .. with one exception: if we know the original sender, and if
             * that no longer is a receiver, then assume the original sender
             * desires to redirect to a different address */
            if(!(f & a_ORIG_SEARCHED)){
               f |= a_ORIG_SEARCHED;
               if(hp->h_mailx_orig_sender != NIL){
                  for(xp = np_orig; xp != NIL; xp = xp->n_flink)
                     if(mx_name_is_same_address(xp, hp->h_mailx_orig_sender)){
                        f |= a_ORIG_FOUND;
                        break;
                     }
               }
            }

            if(!(f & a_ORIG_FOUND))
               goto juseto;
            gf = GCC;
            xpp = &hp->h_cc;
         }else{
juseto:
            gf = GTO;
            xpp = &hp->h_to;
         }
      }

jlink:
      if(!(f & a_LIST_CLASSIFIED))
         continue;

      /* Link it at the end to not loose original sort order */
      if((xp = *xpp) != NIL)
         while(xp->n_flink != NIL)
            xp = xp->n_flink;

      if((nnp->n_blink = xp) != NIL)
         xp->n_flink = nnp;
      else
         *xpp = nnp;
      nnp->n_flink = NIL;
      nnp->n_type = (nnp->n_type & ~GMASK) | gf;
   }

   /* Include formerly unaddressed receivers at the right place */
   if(!(f & a_LIST_CLASSIFIED)){
jclass_ok:
      f |= a_LIST_CLASSIFIED;
      np = np_orig;
      goto jredo;
   }

   /* If afterwards only Cc: data remains, upgrade all of it to To: */
   if(hp->h_to == NIL){
      np = hp->h_cc;
      hp->h_cc = NIL;
      if(!(f & a_ONCE)){
         f |= a_ONCE;
         hp->h_to = NIL;
         goto jredo;
      }else
         for(hp->h_to = np; np != NIL; np = np->n_flink)
            np->n_type = (np->n_type & ~GMASK) | GTO;
   }
   NYD2_OU;
}

static boole
a_crese_do_rt_swap_in(struct header *hp, struct mx_name *the_rt){
   struct mx_name *np;
   char const *rtsi;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   /* We only swap in Reply-To: if it contains only one address, because
    * otherwise the From:/Sender: ambiguation comes into play */
   if(the_rt != NIL && the_rt->n_flink == NIL &&
         (rtsi = ok_vlook(reply_to_swap_in)) != NIL &&
         (np = hp->h_mailx_orig_sender) != NIL){

      rv = TRU1;

      if(*rtsi != '\0'){
         char *cp;

         for(cp = savestr(rtsi); (rtsi = su_cs_sep_c(&cp, ',', TRU1)) != NIL;)
            if(!su_cs_cmp_case(rtsi, "mlist")){
               if(mx_mlist_query(np->n_name, FAL0) == mx_MLIST_OTHER)
                  rv = FAL0;
            }else
               n_err(_("*reply-to-swap-in*: unknown value: %s\n"),
                  n_shexp_quote_cp(rtsi, FAL0));
      }
   }

   NYD2_OU;
   return rv;
}

static void
a_crese_rt_swap_in(struct header *hp, struct mx_name *the_rt){
   NYD2_IN;

   if(a_crese_do_rt_swap_in(hp, the_rt)){
      boole any;
      struct mx_name *np, **xnpp, *xnp;

      np = hp->h_mailx_orig_sender;
      hp->h_mailx_orig_sender = the_rt; /* Unambiguous! */

      for(xnpp = &hp->h_from, any = FAL0;;){
         for(xnp = *xnpp; xnp != NIL; xnp = xnp->n_flink)
            if(mx_name_is_same_address(xnp, np)){
               xnp->n_fullname = the_rt->n_fullname;
               xnp->n_name = the_rt->n_name;
               any = TRU1;
            }
         if(xnpp == &hp->h_from)
            xnpp = &hp->h_sender;
         else if(xnpp == &hp->h_sender)
            xnpp = &hp->h_to;
         else if(xnpp == &hp->h_to)
            xnpp = &hp->h_cc;
         else if(xnpp == &hp->h_cc)
            xnpp = &hp->h_bcc;
         else if(xnpp == &hp->h_bcc)
            xnpp = &hp->h_reply_to;
         else if(xnpp == &hp->h_reply_to)
            xnpp = &hp->h_mft;
         else
            break;
      }

      if(any){
         np = ndup(np, GCC | GSKIN);
         hp->h_cc = cat(hp->h_cc, np);
      }
   }

   NYD2_OU;
}

static void
a_crese_make_ref_and_cs(struct message *mp, struct header *head) /* TODO ASAP*/
{
   char const *ccp;
   char *oldref, *oldmsgid, *newref;
   uz oldreflen = 0, oldmsgidlen = 0, reflen;
   unsigned i;
   struct mx_name *n;
   NYD2_IN;

   oldref = hfield1("references", mp);
   oldmsgid = hfield1("message-id", mp);
   if (oldmsgid == NULL || *oldmsgid == '\0') {
      head->h_ref = NULL;
      goto jleave;
   }

   reflen = 1;
   if (oldref) {
      oldreflen = su_cs_len(oldref);
      reflen += oldreflen + 2;
   }
   if (oldmsgid) {
      oldmsgidlen = su_cs_len(oldmsgid);
      reflen += oldmsgidlen;
   }

   newref = n_alloc(reflen);
   if (oldref != NULL) {
      su_mem_copy(newref, oldref, oldreflen +1);
      if (oldmsgid != NULL) {
         newref[oldreflen++] = ',';
         newref[oldreflen++] = ' ';
         su_mem_copy(newref + oldreflen, oldmsgid, oldmsgidlen +1);
      }
   } else if (oldmsgid)
      su_mem_copy(newref, oldmsgid, oldmsgidlen +1);
   n = extract(newref, GREF);
   n_free(newref);

   /* Limit number of references TODO better on parser side */
   while (n->n_flink != NULL)
      n = n->n_flink;
   for (i = 1; i <= REFERENCES_MAX; ++i) {
      if (n->n_blink != NULL)
         n = n->n_blink;
      else
         break;
   }
   n->n_blink = NIL;
   head->h_ref = n;

   if(ok_blook(reply_in_same_charset) &&
         (ccp = hfield1("content-type", mp)) != NIL &&
         (ccp = mx_mime_param_get("charset", ccp)) != NIL)
      head->h_charset = mx_charsetalias_expand(ccp, FAL0);

jleave:
   NYD2_OU;
}

static int
a_crese_list_reply(int *msgvec, enum header_flags hf){
   struct header head;
   struct message *mp;
   char const *cp, *cp2;
   struct mx_name *rt, *the_rt, *mft, *np;
   enum gfield gf;
   boole local;
   NYD2_IN;

   n_autorec_relax_create();

   n_pstate_err_no = su_ERR_NONE;
   local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);

   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;

jwork_msg:
   mp = &message[*msgvec - 1];
   touch(mp);
   setdot(mp, FAL0);

   su_mem_set(&head, 0, sizeof head);
   head.h_flags = hf;
   head.h_subject = a_crese_sub_edit(mp, FAL0);
   /* XXX Why did i do it so, no fallback to n_header_senderfield_of()? */
   head.h_mailx_orig_sender = mx_header_sender_of(mp, GIDENT | GFULL | gf);
   head.h_mailx_orig_from = lextract(hfield1("from", mp), GIDENT | GFULL | gf);
   head.h_mailx_orig_to = lextract(hfield1("to", mp), GTO | GFULL | gf);
   head.h_mailx_orig_cc = lextract(hfield1("cc", mp), GCC | GFULL | gf);
   head.h_mailx_orig_bcc = lextract(hfield1("bcc", mp), GBCC | GFULL | gf);

   /* First of all check for Reply-To: then Mail-Followup-To:, because these,
    * if honoured, take precedence over anything else.  We will join the
    * resulting list together if so desired.
    * So if we shall honour R-T: or M-F-T:, then these are our receivers! */
   rt = a_crese_reply_to(mp, &the_rt);
   mft = a_crese_mail_followup_to(mp);

   if(rt != NIL || mft != NIL){
      np = cat(rt, mft);
      if(mft != NIL)
         head.h_mft = n_namelist_dup(np, GTO | gf); /* xxx GTO: no "clone"! */

      /* Optionally do not propagate a receiver that originally was in
       * secondary Cc: to the primary To: list */
      if(ok_blook(recipients_in_cc)){
         a_crese_polite_rt_mft_move(mp, &head, np);

         head.h_mailx_raw_cc = n_namelist_dup(head.h_cc, GCC | gf);
         head.h_cc = mx_alternates_remove(head.h_cc, FAL0);
      }else
         head.h_to = np;

      head.h_mailx_raw_to = n_namelist_dup(head.h_to, GTO | gf);
      head.h_to = mx_alternates_remove(head.h_to, FAL0);
#ifdef mx_HAVE_DEVEL
      for(np = head.h_to; np != NULL; np = np->n_flink)
         ASSERT((np->n_type & GMASK) == GTO);
      for(np = head.h_cc; np != NULL; np = np->n_flink)
         ASSERT((np->n_type & GMASK) == GCC);
#endif
      goto jrecipients_done;
   }

   /* Otherwise do the normal From: / To: / Cc: dance */

   if(head.h_mailx_orig_sender != NIL)
      cp2 = head.h_mailx_orig_sender->n_fullname;
   else
      cp2 = n_header_senderfield_of(mp);

   /* Cc: */
   np = NULL;
   if(ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL)
      np = lextract(cp, GCC | gf);
   if((cp = hfield1("cc", mp)) != NULL){
      struct mx_name *x;

      if((x = lextract(cp, GCC | gf)) != NULL)
         np = cat(np, x);
   }
   if(np != NULL){
      head.h_mailx_raw_cc = n_namelist_dup(np, GCC | gf);
      head.h_cc = mx_alternates_remove(np, FAL0);
   }

   /* To: */
   np = NULL;
   if(cp2 != NULL)
      np = lextract(cp2, GTO | gf);
   if(!ok_blook(recipients_in_cc) && (cp = hfield1("to", mp)) != NULL){
      struct mx_name *x;

      if((x = lextract(cp, GTO | gf)) != NULL)
         np = cat(np, x);
   }
   /* Delete my name from reply list, and with it, all my alternate names */
   if(np != NULL){
      head.h_mailx_raw_to = n_namelist_dup(np, GTO | gf);
      np = mx_alternates_remove(np, FAL0);
      /* The user may have send this to himself, don't ignore that */
      if(count(np) == 0){
         np = lextract(cp2, GTO | gf);
         head.h_mailx_raw_to = n_namelist_dup(np, GTO | gf);
      }
   }
   head.h_to = np;

jrecipients_done:
   a_crese_rt_swap_in(&head, the_rt);

   /* For list replies automatically recognize the list address given in the
    * RFC 2369 List-Post: header, so that we will not throw away a possible
    * corresponding receiver: temporarily "`mlist' the List-Post: address" */
   if(hf & HF_LIST_REPLY){
      struct mx_name *lpnp;

      if((lpnp = mx_header_list_post_of(mp)) != NIL){
         if(lpnp == R(struct mx_name*,-1)){
            /* Default is TRU1 because if there are still other addresses that
             * seems to be ok, otherwise we fail anyway */
            if(mx_tty_yesorno(_("List-Post: disallows posting; "
                  "reply nonetheless"), TRU1))
               lpnp = NIL;
            else{
               n_pstate_err_no = su_ERR_DESTADDRREQ;
               msgvec = NIL;
               goto jleave;
            }
         }

         /* A special case has been seen on e.g. ietf-announce@ietf.org:
          * these usually post to multiple groups, with ietf-announce@
          * in List-Post:, but with Reply-To: set to ietf@ietf.org (since
          * -announce@ is only used for announcements, say).
          * So our desire is to honour this request and actively overwrite
          * List-Post: for our purpose; but only if its a single address.
          * However, to avoid ambiguities with users that place themselves in
          * Reply-To: and mailing lists which don't overwrite this (or only
          * extend this, shall such exist), only do so if reply_to exists of
          * a single address which points to the same domain as List-Post: */
         if(rt != NIL && rt->n_flink == NIL &&
               (lpnp == NIL || mx_name_is_same_domain(lpnp, rt)))
            cp = rt->n_name; /* rt is EACM_STRICT tested */
         else
            cp = (lpnp == NIL) ? NIL : lpnp->n_name;

         /* XXX mx_mlist_query_mp()?? */
         if(cp != NIL){
            s8 mlt;

            if((mlt = mx_mlist_query(cp, FAL0)) == mx_MLIST_OTHER)
               head.h_list_post = cp;
         }
      }
   }

   /* In case of list replies we actively sort out any non-list recipient */
   if(hf & HF_LIST_REPLY){
      struct mx_name **nhpp, *nhp, *tail;

      cp = head.h_list_post;

      nhp = *(nhpp = &head.h_to);
      head.h_to = NULL;
j_lt_redo:
      for(tail = NULL; nhp != NULL;){
         s8 mlt;

         np = nhp;
         nhp = nhp->n_flink;

         /* XXX mx_mlist_query_mp()?? */
         if((cp != NIL && !su_cs_cmp_case(cp, np->n_name)) ||
               ((mlt = mx_mlist_query(np->n_name, FAL0)) != mx_MLIST_OTHER &&
                mlt != mx_MLIST_POSSIBLY)){
            if((np->n_blink = tail) != NIL)
               tail->n_flink = np;
            else
               *nhpp = np;
            np->n_flink = NIL;
            tail = np;
         }
      }
      if(nhpp == &head.h_to){
         nhp = *(nhpp = &head.h_cc);
         head.h_cc = NULL;
         goto j_lt_redo;
      }

      /* For `Lreply' only, fail immediately with DESTADDRREQ if there are no
       * receivers at all! */
      if(head.h_to == NULL && head.h_cc == NULL){
         n_err(_("No recipients specified for `Lreply'\n"));
         if(msgvec[1] == 0){
            n_pstate_err_no = su_ERR_DESTADDRREQ;
            msgvec = NULL;
            goto jleave;
         }
         goto jskip_to_next;
      }
   }

   /* Move Cc: to To: as appropriate! */
   if(head.h_to == NULL && (np = head.h_cc) != NULL){
      head.h_cc = NULL;
      for(head.h_to = np; np != NULL; np = np->n_flink)
         np->n_type = (np->n_type & ~GMASK) | GTO;
   }

   a_crese_make_ref_and_cs(mp, &head);

   if(n_mail1((n_MAILSEND_HEADERS_PRINT |
            (hf & HF_RECIPIENT_RECORD ? n_MAILSEND_RECORD_RECIPIENT : 0)),
         &head, mp, NIL, local) != OKAY){
      msgvec = NIL;
      goto jleave;
   }

   if(ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;

jskip_to_next:

   if(*++msgvec != 0){
      /* TODO message (error) ring.., less sleep */
      if(n_psonce & n_PSO_INTERACTIVE){
         fprintf(n_stdout,
            _("Waiting a second before proceeding to the next message..\n"));
         fflush(n_stdout);
         n_msleep(1000, FAL0);
      }
      n_autorec_relax_unroll();
      goto jwork_msg;
   }

jleave:
   n_autorec_relax_gut();

   NYD2_OU;
   return (msgvec == NIL ? n_EXIT_ERR : n_EXIT_OK);
}

static int
(*a_crese_reply_or_Reply(char c))(int *, boole){
   int (*rv)(int*, boole);
   NYD2_IN;

   rv = (ok_blook(flipr) ^ (c == 'R')) ? &a_crese_Reply : &a_crese_reply;
   NYD2_OU;
   return rv;
}

static int
a_crese_reply(int *msgvec, boole recipient_record){
   int rv;
   NYD2_IN;

   rv = a_crese_list_reply(msgvec,
         HF_CMD_reply | (recipient_record ? HF_RECIPIENT_RECORD : HF_NONE));
   NYD2_OU;
   return rv;
}

static int
a_crese_Reply(int *msgvec, boole recipient_record){
   struct header head;
   struct message *mp;
   int *ap;
   enum gfield gf;
   boole local;
   NYD2_IN;

   su_mem_set(&head, 0, sizeof head);

   n_pstate_err_no = su_ERR_NONE;
   local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);

   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;

   mp = n_msgmark1;
   ASSERT(mp != NIL);
   head.h_flags = HF_CMD_Reply;
   head.h_subject = a_crese_sub_edit(mp, FAL0);
   a_crese_make_ref_and_cs(mp, &head);
   head.h_mailx_orig_sender = mx_header_sender_of(mp, GIDENT | GFULL | gf);
   head.h_mailx_orig_from = lextract(hfield1("from", mp), GIDENT | GFULL | gf);
   head.h_mailx_orig_to = lextract(hfield1("to", mp), GTO | GFULL | gf);
   head.h_mailx_orig_cc = lextract(hfield1("cc", mp), GCC | GFULL | gf);
   head.h_mailx_orig_bcc = lextract(hfield1("bcc", mp), GBCC | GFULL | gf);

   for(ap = msgvec; *ap != 0; ++ap){
      struct mx_name *np, *the_rt;

      mp = &message[*ap - 1];
      touch(mp);
      setdot(mp, FAL0);

      if((np = a_crese_reply_to(mp, &the_rt)) == NIL)
         np = lextract(n_header_senderfield_of(mp), GTO | gf);

      if(a_crese_do_rt_swap_in(&head, the_rt)){
         struct mx_name *np_save;

         for(np_save = np; np != NIL; np = np->n_flink)
            if(mx_name_is_same_address(np, head.h_mailx_orig_sender)){
               np->n_fullname = the_rt->n_fullname;
               np->n_name = the_rt->n_name;
            }
         np = np_save;
      }

      head.h_to = cat(head.h_to, np);
   }

   mp = n_msgmark1;

   head.h_mailx_raw_to = n_namelist_dup(head.h_to, GTO | gf);
   head.h_to = mx_alternates_remove(head.h_to, FAL0);

   if(n_mail1(((recipient_record ? n_MAILSEND_RECORD_RECIPIENT : 0) |
            n_MAILSEND_HEADERS_PRINT), &head, mp, NIL, local) != OKAY){
      msgvec = NIL;
      goto jleave;
   }

   if(ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;

jleave:
   NYD2_OU;
   return (msgvec == NIL ? n_EXIT_ERR : n_EXIT_OK);
}

static int
a_crese_fwd(void *vp, boole recipient_record){
   struct header head;
   struct message *mp;
   struct mx_name *recp;
   enum gfield gf;
   boole forward_as_attachment;
   int *msgvec, rv;
   struct mx_cmd_arg *cap;
   struct mx_cmd_arg_ctx *cacp;
   boole local;
   NYD2_IN;

   n_pstate_err_no = su_ERR_NONE;
   local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);

   cacp = vp;
   cap = cacp->cac_arg;
   msgvec = cap->ca_arg.ca_msglist;
   cap = cap->ca_next;
   rv = n_EXIT_ERR;

   if(cap->ca_arg.ca_str.s[0] == '\0'){
      if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
            (n_poption & n_PO_D_V)){
         n_err(_("No recipient specified.\n"));
         mx_cmd_print_synopsis(mx_cmd_firstfit(cacp->cac_desc->cad_name), NIL);
      }
      su_err_set_no(n_pstate_err_no = su_ERR_DESTADDRREQ);
      goto j_leave;
   }

   forward_as_attachment = ok_blook(forward_as_attachment);
   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;
   recp = lextract(cap->ca_arg.ca_str.s, (GTO | GNOT_A_LIST | gf));

   n_autorec_relax_create();

jwork_msg:
   mp = &message[*msgvec - 1];
   touch(mp);
   setdot(mp, FAL0);

   su_mem_set(&head, 0, sizeof head);
   head.h_flags = HF_CMD_forward;
   head.h_to = ndup(recp, (GTO | gf));
   head.h_subject = a_crese_sub_edit(mp, TRU1);
   head.h_mailx_raw_to = n_namelist_dup(recp, GTO | gf);
   head.h_mailx_orig_sender = mx_header_sender_of(mp, GIDENT | GFULL | gf);
   head.h_mailx_orig_from = lextract(hfield1("from", mp), GIDENT | GFULL | gf);
   head.h_mailx_orig_to = lextract(hfield1("to", mp), GTO | GFULL | gf);
   head.h_mailx_orig_cc = lextract(hfield1("cc", mp), GCC | GFULL | gf);
   head.h_mailx_orig_bcc = lextract(hfield1("bcc", mp), GBCC | GFULL | gf);

   if(forward_as_attachment){
      head.h_attach = n_autorec_calloc(1, sizeof *head.h_attach);
      head.h_attach->a_msgno = *msgvec;
      head.h_attach->a_content_description =
         ok_vlook(content_description_forwarded_message);

      if(head.h_mailx_orig_sender != NIL && ok_blook(forward_add_cc)){
         gf = GCC | GSKIN;
         if(ok_blook(fullnames))
            gf |= GFULL;
         head.h_cc = ndup(head.h_mailx_orig_sender, gf);
      }
   }

   if(n_mail1((n_MAILSEND_IS_FWD |
            (recipient_record ? n_MAILSEND_RECORD_RECIPIENT : 0) |
            n_MAILSEND_HEADERS_PRINT), &head,
         (forward_as_attachment ? NIL : mp), NIL, local) != OKAY)
      goto jleave;

   if(*++msgvec != 0){
      /* TODO message (error) ring.., less sleep */
      if(n_psonce & n_PSO_INTERACTIVE){
         fprintf(n_stdout,
            _("Waiting a second before proceeding to the next message..\n"));
         fflush(n_stdout);
         n_msleep(1000, FAL0);
      }
      n_autorec_relax_unroll();
      goto jwork_msg;
   }

   rv = n_EXIT_OK;
jleave:
   n_autorec_relax_gut();
j_leave:
   NYD2_OU;
   return rv;
}

static int
a_crese_resend1(void *vp, boole add_resent){
   struct mx_url url, *urlp = &url;
   struct header head;
   struct mx_name *myto, *myrawto;
   boole mta_isexe;
   enum gfield gf;
   int *msgvec, rv, *ip;
   struct mx_cmd_arg *cap;
   struct mx_cmd_arg_ctx *cacp;
   boole local;
   NYD2_IN;

   local = ((n_pstate & n_PS_ARGMOD_LOCAL) != 0);
   cacp = vp;
   cap = cacp->cac_arg;
   msgvec = cap->ca_arg.ca_msglist;
   cap = cap->ca_next;
   rv = 1;
   n_pstate_err_no = su_ERR_DESTADDRREQ;

   if(cap->ca_arg.ca_str.s[0] == '\0'){
      if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) || (n_poption & n_PO_D_V))
jedar:
         n_err(_("No recipient specified.\n"));
      goto jleave;
   }

   if(!(mta_isexe = mx_sendout_mta_url(urlp))){
      n_pstate_err_no = su_ERR_INVAL;
      goto jleave;
   }
   mta_isexe = (mta_isexe != TRU1);

   gf = ok_blook(fullnames) ? GFULL | GSKIN : GSKIN;

   myrawto = nalloc(cap->ca_arg.ca_str.s, GTO | gf | GNOT_A_LIST | GNULL_OK);
   if(myrawto == NIL)
      goto jedar;

   su_mem_set(&head, 0, sizeof head);
   head.h_to = n_namelist_dup(myrawto, myrawto->n_type);
   /* C99 */{
      s8 snderr;

      snderr = 0;
      myto = n_namelist_vaporise_head(&head, FAL0, !ok_blook(posix),
            (EACM_NORMAL | EACM_DOMAINCHECK |
               (mta_isexe ? EACM_NONE : EACM_NONAME | EACM_NONAME_OR_FAIL)),
            &snderr);

      if(snderr < 0){
         n_err(_("Some addressees were classified as \"hard error\"\n"));
         n_pstate_err_no = su_ERR_PERM;
         goto jleave;
      }
      if(myto == NIL)
         goto jedar;
   }

   n_autorec_relax_create();
   for(ip = msgvec; *ip != 0; ++ip){
      struct message *mp;

      mp = &message[*ip - 1];
      touch(mp);
      setdot(mp, FAL0);

      su_mem_set(&head, 0, sizeof head);
      head.h_flags = HF_CMD_resend;
      head.h_to = myto;
      head.h_mailx_raw_to = myrawto;
      head.h_mailx_orig_sender = mx_header_sender_of(mp, GIDENT | GFULL | gf);
      head.h_mailx_orig_from = lextract(hfield1("from", mp), GIDENT|GFULL|gf);
      head.h_mailx_orig_to = lextract(hfield1("to", mp), GTO | GFULL | gf);
      head.h_mailx_orig_cc = lextract(hfield1("cc", mp), GCC | GFULL | gf);
      head.h_mailx_orig_bcc = lextract(hfield1("bcc", mp), GBCC | GFULL | gf);

      if(n_resend_msg(mp, urlp, &head, add_resent, local) != OKAY){
         /* n_autorec_relax_gut(); XXX but is handled automatically? */
         goto jleave;
      }
      n_autorec_relax_unroll();
   }
   n_autorec_relax_gut();

   n_pstate_err_no = su_ERR_NONE;
   rv = 0;
jleave:
   NYD2_OU;
   return rv;
}

FL int
c_reply(void *vp){
   int rv;
   NYD_IN;

   rv = (*a_crese_reply_or_Reply('r'))(vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_replyall(void *vp){ /* v15-compat */
   int rv;
   NYD_IN;

   rv = a_crese_reply(vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_replysender(void *vp){ /* v15-compat */
   int rv;
   NYD_IN;

   rv = a_crese_Reply(vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_Reply(void *vp){
   int rv;
   NYD_IN;

   rv = (*a_crese_reply_or_Reply('R'))(vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_Lreply(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_list_reply(vp, HF_CMD_Lreply | HF_LIST_REPLY);
   NYD_OU;
   return rv;
}

FL int
c_followup(void *vp){
   int rv;
   NYD_IN;

   rv = (*a_crese_reply_or_Reply('r'))(vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_followupall(void *vp){ /* v15-compat */
   int rv;
   NYD_IN;

   rv = a_crese_reply(vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_followupsender(void *vp){ /* v15-compat */
   int rv;
   NYD_IN;

   rv = a_crese_Reply(vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_Followup(void *vp){
   int rv;
   NYD_IN;

   rv = (*a_crese_reply_or_Reply('R'))(vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_Lfollowup(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_list_reply(vp,
         HF_CMD_Lreply | HF_LIST_REPLY | HF_RECIPIENT_RECORD);
   NYD_OU;
   return rv;
}

FL int
c_forward(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_fwd(vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_Forward(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_fwd(vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_resend(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_resend1(vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_Resend(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_resend1(vp, FAL0);
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
