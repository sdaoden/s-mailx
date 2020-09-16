/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-resend.h.
 *
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
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
#define su_FILE cmd_resend
#define mx_SOURCE
#define mx_SOURCE_CMD_RESEND

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>
#include <su/time.h>

#include "mx/attachments.h"
#include "mx/cmd.h"
#include "mx/cmd-ali-alt.h"
#include "mx/cmd-charsetalias.h"
#include "mx/cmd-mlist.h"
#include "mx/mime-param.h"
#include "mx/names.h"
#include "mx/url.h"
#include "mx/tty.h"

#include "mx/cmd-resend.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Modify subject to begin with Re:/Fwd: if it does not already */
static char *a_crese_sub_edit(struct message *mp, boole isfwd);

/* We honoured Mail-Followup-To:, but *recipients-in-cc* is set so try to keep
 * "secondary" addressees in Cc: */
static void a_crese_polite_move(struct message *mp, struct header *hp,
      struct mx_name *np);

/* *reply-to-swap-in* */
static boole a_crese_do_rt_swap_in(struct header *hp, struct mx_name *the_rt);
static void a_crese_rt_swap_in(struct header *hp, struct mx_name *the_rt);

/* References and charset, as appropriate */
static void a_crese_make_ref_and_cs(struct message *mp, struct header *head);

/* `reply' and `Lreply' workhorse */
static int a_crese_list_reply(void *vp, enum header_flags hf);

/* Get PTF to implementation of command c (i.e., take care for *flipr*) */
SINLINE int (*a_crese_reply_or_Reply(char c))(void*, boole);

/* Reply to a single message.  Extract each name from the message header and
 * send them off to mail1() */
static int a_crese_reply(void *vp, boole recipient_record);

/* Reply to a series of messages by simply mailing to the senders and not
 * messing around with the To: and Cc: lists as in normal reply */
static int a_crese_Reply(void *vp, boole recipient_record);

/* Forward a message to a new recipient, in the sense of RFC 2822 */
static int a_crese_fwd(void *vp, boole recipient_record);

/* Do the real work of resending */
static int a_crese_resend1(void *v, boole add_resent);

static char *
a_crese_sub_edit(struct message *mp, boole isfwd){
   char *newsubj;
   char const *subj;
   NYD2_IN;

   subj = hfield1("subject", mp);
   if(subj == NIL)
      subj = su_empty;
   if((n_psonce & n_PSO_INTERACTIVE) && *subj == '\0')
      subj = _("(no subject)");

   if(*(newsubj = mx_header_subject_edit(subj, (isfwd
         ? (mx_HEADER_SUBJECT_EDIT_DECODE_MIME |
            mx_HEADER_SUBJECT_EDIT_TRIM_FWD |
            mx_HEADER_SUBJECT_EDIT_PREPEND_FWD)
         : (mx_HEADER_SUBJECT_EDIT_DECODE_MIME |
            mx_HEADER_SUBJECT_EDIT_TRIM_RE |
            mx_HEADER_SUBJECT_EDIT_PREPEND_RE)))) == '\0')
      newsubj = NIL;

   NYD2_OU;
   return newsubj;
}

static void
a_crese_polite_move(struct message *mp, struct header *hp, struct mx_name *np){
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

      /* If this recipient came in only via R-T: or M-F-T:, place her/him/it in
       * To: due to lack of a better place.  But only if To: is not empty after
       * all formerly present recipients have been worked, to avoid that yet
       * unaddressed recipients propagate to To: whereas formerly addressed
       * ones end in Cc: .. */
      if(f & a_LIST_CLASSIFIED){
         if(f & a_SEEN_TO){
            /* .. with one exception: if we know the original sender, and if
             * that no longer is a recipient, then assume the original sender
             * desires to redirect to a different address */
            if(!(f & a_ORIG_SEARCHED)){
               f |= a_ORIG_SEARCHED;
               if(hp->h_mailx_eded_sender != NIL){
                  for(xp = np_orig; xp != NIL; xp = xp->n_flink)
                     if(mx_name_is_same_address(xp, hp->h_mailx_eded_sender)){
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

   /* Include formerly unaddressed recipients at the right place */
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
         (np = hp->h_mailx_eded_sender) != NIL){
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

      np = hp->h_mailx_eded_sender;

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
         np = ndup(np, GCC);
         hp->h_cc = cat(hp->h_cc, np);
      }
   }

   NYD2_OU;
}

static void
a_crese_make_ref_and_cs(struct message *mp, struct header *head){
   char const *ccp;
   NYD2_IN;

   mx_header_setup_references(head, mp);

   if(ok_blook(reply_in_same_charset) &&
         (ccp = hfield1("content-type", mp)) != NIL &&
         (ccp = mx_mime_param_get("charset", ccp)) != NIL)
      head->h_charset = mx_charsetalias_expand(ccp, FAL0);

   NYD2_OU;
}

static int
a_crese_list_reply(void *vp, enum header_flags hf){
   enum a_flags{
      a_NONE,
      a_LOCAL = 1u<<0,
      a_TICKED_ONCE = 1u<<1,

      a_PERSIST_MASK = a_LOCAL | a_TICKED_ONCE,

      a_REC_IN_CC = 1u<<8,
      a_MARK_ANSWER = 1u<<9,
      a_FULLNAMES = 1u<<10
   };

   struct header head;
   struct message *mp;
   char const *cp, *cp2;
   struct mx_name *the_rt, *mft, *np;
   BITENUM(u32,a_flags) flags;
   int *msgvec;
   struct mx_cmd_arg_ctx *cacp;
   NYD2_IN;

   su_mem_bag_auto_snap_create(su_MEM_BAG_SELF);
   n_pstate_err_no = su_ERR_NONE;

   cacp = vp;
   msgvec = cacp->cac_arg->ca_arg.ca_msglist;
   flags = (cacp->cac_scope == mx_SCOPE_LOCAL) ? a_LOCAL : a_NONE;

jwork_msg:
   mp = &message[*msgvec - 1];
   touch(mp);
   setdot(mp, FAL0);

   /* */
   if(!(flags & a_LOCAL) || !(flags & a_TICKED_ONCE)){
      flags &= a_PERSIST_MASK;
      if(ok_blook(recipients_in_cc))
         flags |= a_REC_IN_CC;
      if(ok_blook(markanswered))
         flags |= a_MARK_ANSWER;

      if(ok_blook(fullnames))
         flags |= a_FULLNAMES;
   }
   flags |= a_TICKED_ONCE;

   STRUCT_ZERO(struct header, &head);
   head.h_flags = hf;
   head.h_subject = a_crese_sub_edit(mp, FAL0);
   mx_header_setup_pseudo_orig(&head, mp);
   the_rt = mx_header_get_reply_to(mp, &head, FAL0);

   if((mft = mx_header_get_mail_followup_to(mp)) != NIL){
      head.h_mft = mft = n_namelist_dup(mft, GTO);

      /* Optionally do not propagate a recipient that originally was in
       * secondary Cc: to the primary To: list */
      if(flags & a_REC_IN_CC){
         a_crese_polite_move(mp, &head, mft);
         head.h_mailx_raw_cc = n_namelist_dup(head.h_cc, GCC);
         head.h_cc = mx_alternates_remove(head.h_cc, FAL0);
      }else
         head.h_to = mft;

      head.h_mailx_raw_to = n_namelist_dup(head.h_to, GTO);
      head.h_to = mx_alternates_remove(head.h_to, FAL0);
#ifdef mx_HAVE_DEVEL
      for(np = head.h_to; np != NIL; np = np->n_flink)
         ASSERT((np->n_type & GMASK) == GTO);
      for(np = head.h_cc; np != NIL; np = np->n_flink)
         ASSERT((np->n_type & GMASK) == GCC);
#endif
      goto jrecipients_done;
   }

   /* Cc: */
   np = NIL;
   if((flags & a_REC_IN_CC) && (cp = hfield1("to", mp)) != NIL)
      np = mx_name_parse(cp, GCC | GTRASH_HACK);
   if((cp = hfield1("cc", mp)) != NIL)
      np = cat(np, mx_name_parse(cp, GCC | GTRASH_HACK));
   if(np != NIL){
      head.h_mailx_raw_cc = n_namelist_dup(np, GCC);
      head.h_cc = mx_alternates_remove(np, FAL0);
   }

   /* To: */
   /* Delete my name from reply list, and with it, all my alternate names */
   if(np != NIL){
      head.h_mailx_raw_to = n_namelist_dup(np, GTO | GTRASH_HACK);
      np = mx_alternates_remove(np, FAL0);
      /* The user may have sent this to himself, do not ignore that */
      if(count(np) == 0)
         head.h_mailx_raw_to = n_namelist_dup(head.h_mailx_eded_sender, GTO);
   }
   head.h_to = np;

jrecipients_done:
   a_crese_rt_swap_in(&head, the_rt);

   /* For list replies automatically recognize the list address given in the
    * RFC 2369 List-Post: header, so that we will not throw away a possible
    * corresponding recipient: temporarily "`mlist' the List-Post: address" */
   if(hf & HF_LIST_REPLY){
      struct mx_name *lpnp;

      if((lpnp = mx_header_list_post_of(mp)) != NIL){
         if(lpnp == R(struct mx_name*,-1)){
            /* Default is TRU1 because if there are still other addresses that
             * seems to be ok, otherwise we fail anyway */
            if(mx_tty_yesorno(_("List-Post: header disallows posting; "
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
         if(the_rt != NIL && the_rt->n_flink == NIL &&
               (lpnp == NIL || mx_name_is_same_domain(lpnp, the_rt)))
            cp = the_rt->n_name; /* rt is mx_EACM_STRICT tested */
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
       * recipients at all! */
      if(head.h_to == NIL && head.h_cc == NIL){
         n_err(_("No recipients specified for `Lreply'\n"));
         if(msgvec[1] == 0){
            n_pstate_err_no = su_ERR_DESTADDRREQ;
            msgvec = NIL;
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
         cacp->cac_scope, &head, mp, NIL) != OKAY){
      msgvec = NIL;
      goto jleave;
   }

   if((flags & a_MARK_ANSWER) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;

jskip_to_next:
   if(*++msgvec != 0){
      su_mem_bag_auto_snap_unroll(su_MEM_BAG_SELF);

      if(mx_tty_yesorno(NIL, TRU1))
         goto jwork_msg;
   }

jleave:
   su_mem_bag_auto_snap_gut(su_MEM_BAG_SELF);

   NYD2_OU;
   return (msgvec == NIL ? su_EX_ERR : su_EX_OK);
}

SINLINE int
(*a_crese_reply_or_Reply(char c))(void*, boole){
   int (*rv)(void*, boole);
   NYD2_IN;

   rv = (ok_blook(flipr) ^ (c == 'R')) ? &a_crese_Reply : &a_crese_reply;

   NYD2_OU;
   return rv;
}

static int
a_crese_reply(void *vp, boole recipient_record){
   int rv;
   NYD2_IN;

   rv = a_crese_list_reply(vp,
         HF_CMD_reply | (recipient_record ? HF_RECIPIENT_RECORD : HF_NONE));

   NYD2_OU;
   return rv;
}

static int
a_crese_Reply(void *vp, boole recipient_record){
   struct header head;
   int *ap;
   struct mx_name *the_rt;
   struct message *mp;
   struct mx_cmd_arg_ctx *cacp;
   NYD2_IN;

   STRUCT_ZERO(struct header, &head);
   n_pstate_err_no = su_ERR_NONE;

   cacp = vp;
   mp = n_msgmark1;
   ASSERT(mp != NIL);
   head.h_flags = HF_CMD_Reply;
   head.h_subject = a_crese_sub_edit(mp, FAL0);
   a_crese_make_ref_and_cs(mp, &head);
   mx_header_setup_pseudo_orig(&head, mp);
   the_rt = mx_header_get_reply_to(mp, &head, FAL0);

   for(ap = cacp->cac_arg->ca_arg.ca_msglist; *ap != 0; ++ap){
      struct mx_name *np;

      mp = &message[*ap - 1];
      touch(mp);
      setdot(mp, FAL0);

      if(mp != n_msgmark1)
         the_rt = cat(the_rt, mx_header_get_reply_to(mp, &head, TRU1));

np=NIL;
#if 0
FIXME
      if(a_crese_do_rt_swap_in(&head, the_rt)){
         struct mx_name *np_save;

         for(np_save = np; np != NIL; np = np->n_flink)
            if(mx_name_is_same_address(np, head.h_mailx_orig_sender)){
               np->n_fullname = the_rt->n_fullname;
               np->n_name = the_rt->n_name;
            }
         np = np_save;
      }
#endif

      head.h_to = cat(head.h_to, np);
   }

   mp = n_msgmark1;

   head.h_mailx_raw_to = n_namelist_dup(head.h_to, GTO);
   head.h_to = mx_alternates_remove(head.h_to, FAL0);

   if(n_mail1(((recipient_record ? n_MAILSEND_RECORD_RECIPIENT : 0) |
            n_MAILSEND_HEADERS_PRINT), cacp->cac_scope,
            &head, mp, NIL) != OKAY){
      ap = NIL;
      goto jleave;
   }

   if(ok_blook(markanswered) && !(mp->m_flag & MANSWERED))
      mp->m_flag |= MANSWER | MANSWERED;

jleave:
   NYD2_OU;
   return (ap == NIL ? su_EX_ERR : su_EX_OK);
}

static int
a_crese_fwd(void *vp, boole recipient_record){
   enum a_flags{
      a_NONE,
      a_LOCAL = 1u<<0,
      a_TICKED_ONCE = 1u<<1,

      a_PERSIST_MASK = a_LOCAL | a_TICKED_ONCE,

      a_AS_ATTACHMENT = 1u<<8,
      a_ADD_CC = 1u<<9,
      a_FULLNAMES = 1u<<10
   };

   struct header head;
   struct message *mp;
   struct mx_name *recp;
   int *msgvec, rv;
   struct mx_cmd_arg *cap;
   BITENUM(u32,a_flags) flags;
   struct mx_cmd_arg_ctx *cacp;
   NYD2_IN;

   n_pstate_err_no = su_ERR_NONE;
   cacp = vp;
   flags = (cacp->cac_scope == mx_SCOPE_LOCAL) ? a_LOCAL : a_NONE;
   cap = cacp->cac_arg;
   msgvec = cap->ca_arg.ca_msglist;
   cap = cap->ca_next;
   rv = su_EX_ERR;
   UNINIT(recp, NIL);

   if(cap->ca_arg.ca_str.s[0] == '\0'){
      if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
            (n_poption & n_PO_D_V)){
         n_err(_("No recipient specified.\n"));
         mx_cmd_print_synopsis(mx_cmd_by_arg_desc(cacp->cac_desc), NIL);
      }
      su_err_set(n_pstate_err_no = su_ERR_DESTADDRREQ);
      goto j_leave;
   }

   recp_base = lextract(cap->ca_arg.ca_str.s, (GTO | GNOT_A_LIST));

   su_mem_bag_auto_snap_create(su_MEM_BAG_SELF);
jwork_msg:
   mp = &message[*msgvec - 1];
   touch(mp);
   setdot(mp, FAL0);

   /* */
   if(!(flags & a_LOCAL) || !(flags & a_TICKED_ONCE)){
      flags &= a_PERSIST_MASK;
      if(ok_blook(forward_as_attachment))
         flags |= a_AS_ATTACHMENT;
      if(ok_blook(forward_add_cc))
         flags |= a_ADD_CC;
      if(ok_blook(fullnames))
         flags |= a_FULLNAMES;
   }
   flags |= a_TICKED_ONCE;

   STRUCT_ZERO(struct header, &head);
   head.h_flags = HF_CMD_forward;
   head.h_to = ndup(recp, GTO);
   head.h_subject = a_crese_sub_edit(mp, TRU1);
   head.h_mailx_raw_to = n_namelist_dup(recp, GTO);
   mx_header_setup_pseudo_orig(&head, mp);

   if(flags & a_AS_ATTACHMENT){
      head.h_attach = su_AUTO_TCALLOC(struct mx_attachment, 1);
      head.h_attach->a_msgno = *msgvec;
      head.h_attach->a_content_description =
         ok_vlook(content_description_forwarded_message);

      /* Otherwise compose mode will do it when quoting the message */
      if(flags & a_ADD_CC)
         head.h_cc = ndup(mx_header_get_reply_to(mp, NIL, FAL0), GCC);
   }

   if(n_mail1((n_MAILSEND_IS_FWD |
            (recipient_record ? n_MAILSEND_RECORD_RECIPIENT : 0) |
            n_MAILSEND_HEADERS_PRINT),
            cacp->cac_scope, &head,
         ((flags & a_AS_ATTACHMENT) ? NIL : mp), NIL) != OKAY)
      goto jleave;

   if(*++msgvec != 0){
      su_mem_bag_auto_snap_unroll(su_MEM_BAG_SELF);

      if(mx_tty_yesorno(NIL, TRU1))
         goto jwork_msg;
   }

   rv = su_EX_OK;
jleave:
   su_mem_bag_auto_snap_gut(su_MEM_BAG_SELF);

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
   int *msgvec, rv, *ip;
   struct mx_cmd_arg *cap;
   struct mx_cmd_arg_ctx *cacp;
   NYD2_IN;

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

   myrawto = mx_name_parse_as_one(cap->ca_arg.ca_str.s, GTO);
   if(myrawto == NIL)
      goto jedar;

   su_mem_set(&head, 0, sizeof head);
   head.h_to = n_namelist_dup(myrawto, myrawto->n_type);
   /* C99 */{
      s8 snderr;

      snderr = 0;
      myto = n_namelist_vaporise_head(&head, (mx_EACM_NORMAL |
                  mx_EACM_DOMAINCHECK |
                  (mta_isexe ? mx_EACM_NONE
                  : mx_EACM_NONAME | mx_EACM_NONAME_OR_FAIL)),
            &snderr);

      if(snderr < 0){
         n_err(_("Some addressees were classified as \"hard error\"\n"));
         n_pstate_err_no = su_ERR_PERM;
         goto jleave;
      }
      if(myto == NIL)
         goto jedar;
   }

   su_mem_bag_auto_snap_create(su_MEM_BAG_SELF);
   for(ip = msgvec; *ip != 0; ++ip){
      struct message *mp;

      mp = &message[*ip - 1];
      touch(mp);
      setdot(mp, FAL0);

      su_mem_set(&head, 0, sizeof head);
      head.h_flags = HF_CMD_resend;
      head.h_to = myto;
      head.h_mailx_raw_to = myrawto;
      mx_header_setup_pseudo_orig(&head, mp);

      if(n_resend_msg(cacp->cac_scope, mp, urlp, &head, add_resent) == OKAY &&
            *++ip == 0){
         n_pstate_err_no = su_ERR_NONE;
         rv = 0;
         break;
      }
      su_mem_bag_auto_snap_unroll(su_MEM_BAG_SELF);
   }
   su_mem_bag_auto_snap_gut(su_MEM_BAG_SELF);

jleave:
   NYD2_OU;
   return rv;
}

int
c_reply(void *vp){
   int rv;
   NYD_IN;

   rv = (*a_crese_reply_or_Reply('r'))(vp, FAL0);

   NYD_OU;
   return rv;
}

int
c_replyall(void *vp){ /* v15-compat */
   int rv;
   NYD_IN;

   rv = a_crese_reply(vp, FAL0);

   NYD_OU;
   return rv;
}

int
c_replysender(void *vp){ /* v15-compat */
   int rv;
   NYD_IN;

   rv = a_crese_Reply(vp, FAL0);

   NYD_OU;
   return rv;
}

int
c_Reply(void *vp){
   int rv;
   NYD_IN;

   rv = (*a_crese_reply_or_Reply('R'))(vp, FAL0);

   NYD_OU;
   return rv;
}

int
c_Lreply(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_list_reply(vp, HF_CMD_Lreply | HF_LIST_REPLY);

   NYD_OU;
   return rv;
}

int
c_followup(void *vp){
   int rv;
   NYD_IN;

   rv = (*a_crese_reply_or_Reply('r'))(vp, TRU1);

   NYD_OU;
   return rv;
}

int
c_followupall(void *vp){ /* v15-compat */
   int rv;
   NYD_IN;

   rv = a_crese_reply(vp, TRU1);

   NYD_OU;
   return rv;
}

int
c_followupsender(void *vp){ /* v15-compat */
   int rv;
   NYD_IN;

   rv = a_crese_Reply(vp, TRU1);

   NYD_OU;
   return rv;
}

int
c_Followup(void *vp){
   int rv;
   NYD_IN;

   rv = (*a_crese_reply_or_Reply('R'))(vp, TRU1);

   NYD_OU;
   return rv;
}

int
c_Lfollowup(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_list_reply(vp,
         HF_CMD_Lreply | HF_LIST_REPLY | HF_RECIPIENT_RECORD);

   NYD_OU;
   return rv;
}

int
c_forward(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_fwd(vp, FAL0);

   NYD_OU;
   return rv;
}

int
c_Forward(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_fwd(vp, TRU1);

   NYD_OU;
   return rv;
}

int
c_resend(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_resend1(vp, TRU1);

   NYD_OU;
   return rv;
}

int
c_Resend(void *vp){
   int rv;
   NYD_IN;

   rv = a_crese_resend1(vp, FAL0);

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_RESEND
/* s-it-mode */
