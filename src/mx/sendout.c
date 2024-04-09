/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Message sending lifecycle, header composing, etc.
 *@ TODO total mess
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE sendout
#define mx_SOURCE
#define mx_SOURCE_SENDOUT

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/time.h>

#include "mx/attachments.h"
#include "mx/child.h"
#include "mx/cmd.h"
#include "mx/cmd-mlist.h"
#include "mx/compat.h"
#include "mx/cred-auth.h"
#include "mx/fexpand.h"
#include "mx/file-locks.h"
#include "mx/file-streams.h"
#include "mx/iconv.h"
#include "mx/mime.h"
#include "mx/mime-enc.h"
#include "mx/mime-param.h"
#include "mx/mime-probe.h"
#include "mx/mime-type.h"
#include "mx/names.h"
#include "mx/net-smtp.h"
#include "mx/privacy.h"
#include "mx/random.h"
#include "mx/sigs.h"
#include "mx/time.h"
#include "mx/tty.h"
#include "mx/url.h"

/* TODO fake; old style; wrong sortage */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

#undef SEND_LINESIZE
#define SEND_LINESIZE \
   ((1024 / mx_B64_ENC_INPUT_PER_LINE) * mx_B64_ENC_INPUT_PER_LINE)

enum a_sendout_addrline_flags{
   a_SENDOUT_AL_INC_INVADDR = 1<<0, /* _Do_ include invalid addresses */
   a_SENDOUT_AL_DOMIME = 1<<1,      /* Perform MIME conversion */
   a_SENDOUT_AL_COMMA = GCOMMA,
   a_SENDOUT_AL_FILES = GFILES,
   a_SENDOUT__AL_GMASK = a_SENDOUT_AL_COMMA | a_SENDOUT_AL_FILES
};
CTA(!(a_SENDOUT__AL_GMASK & (a_SENDOUT_AL_INC_INVADDR|a_SENDOUT_AL_DOMIME)),
   "Code-required condition not satisfied but actual bit carrier value");

enum a_sendout_sendwait_flags{
   a_SENDOUT_SWF_NONE,
   a_SENDOUT_SWF_MTA = 1u<<0,
   a_SENDOUT_SWF_PCC = 1u<<1,
   a_SENDOUT_SWF_MASK = a_SENDOUT_SWF_MTA | a_SENDOUT_SWF_PCC
};

struct a_sendout_infix_ctx{
   s32 sic_eno; /* ==su_ERR_NONE = return */
   boole sic_no_body; /* no main part */
   boole sic_gen_ids; /* generate message-id: and content-id: */
   u8 sic__pad[2];
   char const *sic_emsg;
   char const *sic_emsg_arg;
   FILE *sic_encfp;
   FILE *sic_encfp_input;
   struct mx_send_ctx *sic_sctxp;
   char const *sic_mime_boundary;
   struct mx_mime_probe_charset_ctx sic_mpcc;
   struct mx_mime_type_classify_fp_ctx sic_mtcfc; /* Of mainbody */
};

static char const *__sendout_ident; /* TODO temporary; rewrite n_puthead() */
static s8   _sendout_error;

/* */
static u32 a_sendout_sendwait_to_swf(void);

/* *fullnames* appears after command line arguments have been parsed */
static struct mx_name *a_sendout_fullnames_cleanup(struct mx_name *np);

/* */
static boole a_sendout_put_name(char const *line, enum gfield w,
      enum sendaction action, char const *prefix, FILE *fo,
      struct mx_name **xp);

/* Put all entries of the given header list */
static boole        _sendout_header_list(FILE *fo, struct n_header_field *hfp,
                        boole nodisp);

/* There are non-local recipients, collect credentials etc. */
static boole a_sendout_setup_creds(struct mx_send_ctx *scp, boole sign_caps);

/* Prepend a header in front of the collected stuff and return the new file */
static boole a_sendout_infix(struct mx_send_ctx *sctxp, boole dosign);

static s32 a_sendout__infix_attach(struct mx_send_ctx *sctxp,
      struct a_sendout_infix_ctx *sicp);
static s32 a_sendout__infix_heads(struct mx_send_ctx *sctxp,
      struct a_sendout_infix_ctx *sicp);
static s32 a_sendout__infix_dump(struct mx_send_ctx *sctxp,
      struct a_sendout_infix_ctx *sicp);
static s32 a_sendout__infix_file(enum conversion conv, FILE *ofp, FILE *ifp);

/* Place Content-Type:, Content-Transfer-Encoding:, Content-Disposition:
 * unless RFC's allow leaving them off */
static int a_sendout__infix_cd(FILE *ofp, char const *cd, char const *filename,
      struct mx_mime_probe_charset_ctx const *mpccp);
static int a_sendout__infix_ct(FILE *ofp,
      struct mx_mime_type_classify_fp_ctx *mtcfcp);
static int a_sendout__infix_cte(FILE *ofp,
      struct mx_mime_type_classify_fp_ctx *mtcfcp);

/* Check whether Disposition-Notification-To: is desired */
static boole        _check_dispo_notif(struct mx_name *mdn, struct header *hp,
                        FILE *fo);

/* Send mail to a bunch of user names.  The interface is through mail() */
static int a_sendout_sendmail(void *vp, enum n_mailsend_flags msf);

/* Deal with file and pipe addressees */
static struct mx_name *a_sendout_file_a_pipe(struct mx_name *names, FILE *fo,
                     boole *senderror);

/* Record outgoing mail if instructed to do so; in *record* unless to is set */
static boole a_sendout_mightrecord(FILE *fp, struct mx_name *to, boole resend);

static boole a_sendout__savemail(char const *name, FILE *fp, boole resend);

/* Move a message over to non-local recipients (via MTA) */
static boole a_sendout_transfer(struct mx_send_ctx *scp, boole resent,
      boole *senderror);

/* Actual MTA interaction */
static boole a_sendout_mta_start(struct mx_send_ctx *scp);
static char const **a_sendout_mta_file_args(struct mx_name *to,
      struct header *hp);
static void a_sendout_mta_file_debug(struct mx_send_ctx *scp, char const *mta,
      char const **args);
static boole a_sendout_mta_test(struct mx_send_ctx *scp, char const *mta);

/* Create a (Message-ID|Content): header field (via *message-id*) */
static char const *a_sendout_random_id(struct header *hp, boole msgid);

/* Format the given header line to not exceed 72 characters */
static boole a_sendout_put_addrline(char const *hname, struct mx_name *np,
               FILE *fo, enum a_sendout_addrline_flags saf);

/* Rewrite a message for resending, adding the Resent-Headers */
static boole a_sendout_infix_resend(struct header *hp, FILE *fi, FILE *fo,
      struct message *mp, struct mx_name *to, int add_resent);

static u32
a_sendout_sendwait_to_swf(void){ /* TODO should happen at var assign time */
   char *buf;
   u32 rv;
   char const *cp;
   NYD2_IN;

   if((cp = ok_vlook(sendwait)) == NIL)
      rv = a_SENDOUT_SWF_NONE;
   else if(*cp == '\0')
      rv = a_SENDOUT_SWF_MASK;
   else{
      rv = a_SENDOUT_SWF_NONE;

      for(buf = savestr(cp); (cp = su_cs_sep_c(&buf, ',', TRU1)) != NIL;){
         if(!su_cs_cmp_case(cp, "mta"))
            rv |= a_SENDOUT_SWF_MTA;
         else if(!su_cs_cmp_case(cp, "pcc"))
            rv |= a_SENDOUT_SWF_PCC;
         else if(n_poption & n_PO_D_V)
            n_err(_("Unknown *sendwait* content: %s\n"),
               n_shexp_quote_cp(cp, FAL0));
      }
   }
   NYD2_OU;
   return rv;
}

static struct mx_name *
a_sendout_fullnames_cleanup(struct mx_name *np){
   struct mx_name *xp;
   NYD2_IN;

   for(xp = np; xp != NULL; xp = xp->n_flink){
      xp->n_type &= ~(GFULL | GFULLEXTRA);
      xp->n_fullname = xp->n_name;
      xp->n_fullextra = NULL;
   }
   NYD2_OU;
   return np;
}

static boole
a_sendout_put_name(char const *line, enum gfield w, enum sendaction action,
   char const *prefix, FILE *fo, struct mx_name **xp){
   boole rv;
   struct mx_name *np;
   NYD_IN;

   np = (w & GNOT_A_LIST ? n_extract_single : lextract)(line, GEXTRA | GFULL);
   if(xp != NIL)
      *xp = np;

   if(np == NIL){
      su_err_set(su_ERR_INVAL);
      rv = FAL0;
   }else
      rv = a_sendout_put_addrline(prefix, np, fo, ((w & GCOMMA) |
            ((action != SEND_TODISP) ? a_SENDOUT_AL_DOMIME : 0)));

   NYD_OU;
   return rv;
}

static boole
_sendout_header_list(FILE *fo, struct n_header_field *hfp, boole nodisp){
   boole rv;
   NYD2_IN;

   for(rv = TRU1; hfp != NIL; hfp = hfp->hf_next){
      if(fwrite(hfp->hf_dat, sizeof(char), hfp->hf_nl, fo) != hfp->hf_nl ||
            putc(':', fo) == EOF || putc(' ', fo) == EOF)
         goto jerr;
      if(mx_xmime_write(hfp->hf_dat + hfp->hf_nl +1, hfp->hf_bl, fo,
               (!nodisp ? CONV_NONE : CONV_TOHDR),
               (!nodisp ? mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT
                : mx_MIME_DISPLAY_ICONV), NIL, NIL) < 0){
         rv = FAL0;
         break;
      }
      if(putc('\n', fo) == EOF){
jerr:
         su_err_by_errno();
         rv = FAL0;
         break;
      }
   }

   NYD2_OU;
   return rv;
}

static boole
a_sendout_setup_creds(struct mx_send_ctx *scp, boole sign_caps){
   boole rv;
   char *shost, *from;
   NYD_IN;

   rv = FAL0;
   shost = ok_vlook(smtp_hostname);
   from = ((sign_caps || shost == NIL) ? skin(myorigin(scp->sc_hp)) : NIL);

   if(sign_caps){
      if(from == NIL){
#ifdef mx_HAVE_SMIME
         n_err(_("No *from* address for signing specified\n"));
         goto jleave;
#endif
      }
      scp->sc_signer.l = su_cs_len(scp->sc_signer.s = from);
   }

   /* file:// and test:// MTAs do not need credentials */
   if(scp->sc_urlp->url_cproto == CPROTO_NONE){
      rv = TRU1;
      goto jleave;
   }

#ifdef mx_HAVE_SMTP
   if(shost == NIL){
      if(from == NIL){
         n_err(_("Your configuration requires a *from* address, "
            "but none was given\n"));
         goto jleave;
      }
      scp->sc_urlp->url_u_h.l = su_cs_len(scp->sc_urlp->url_u_h.s = from);
   }else
      __sendout_ident = scp->sc_urlp->url_u_h.s;

   if(!mx_cred_auth_lookup(scp->sc_credp, scp->sc_urlp))
      goto jleave;

   rv = TRU1;
#endif

jleave:
   NYD_OU;
   return rv;
}

static boole
a_sendout_infix(struct mx_send_ctx *sctxp, boole dosign){ /* {{{ */
   struct mx_fs_tmp_ctx *fstcp;
   struct a_sendout_infix_ctx sic;
   NYD_IN;

   STRUCT_ZERO(struct a_sendout_infix_ctx, &sic);
   mx_mime_probe_charset_ctx_setup(&sic.sic_mpcc);
   sic.sic_sctxp = sctxp;

   /* C99 */{
      char const *cp;

      if((cp = ok_vlook(stealthmua)) == NIL || !su_cs_cmp(cp, "noagent"))
         sic.sic_gen_ids = TRU1;
   }

   /* Classify body first */
   if(fsize(sctxp->sc_input) == 0)
      sic.sic_no_body = TRU1;
   else{
      mx_mime_type_classify_fp_setup(&sic.sic_mtcfc, !dosign, NIL, &sic.sic_mpcc);
      if(!mx_mime_type_classify_fp(&sic.sic_mtcfc, sctxp->sc_input)){
#ifdef mx_HAVE_ICONV
jedraft:
#endif
         sic.sic_emsg = N_("Mail draft MIME classification failed");
         goto jxerr;
      }

      /* Need iconv?  And re-classify the converted body! */
#ifdef mx_HAVE_ICONV
      if(sic.sic_mtcfc.mtcfc_do_iconv){
         FILE *nfp;

         nfp = NIL;
         if((sic.sic_eno = mx_mime_charset_iter_onetime_fp(&nfp, sctxp->sc_input,
                  &sic.sic_mtcfc, sctxp->sc_hp->h_charset, &sic.sic_emsg)
               ) != su_ERR_NONE)
            goto jerr;

         /* If success came only due to *mime-force-sendout*, take "as-is"! */
         if(nfp != NIL){
            char const *ics, *cs;

            mx_fs_close(sctxp->sc_input);
            sctxp->sc_input = nfp;

            ics = sic.sic_mtcfc.mtcfc_input_charset;
            cs = sic.sic_mtcfc.mtcfc_charset;
            mx_mime_type_classify_fp_setup(&sic.sic_mtcfc, !dosign, NIL, &sic.sic_mpcc);
            if(!mx_mime_type_classify_fp(&sic.sic_mtcfc, sctxp->sc_input))
               goto jedraft;
            sic.sic_mtcfc.mtcfc_input_charset = ics;
            sic.sic_mtcfc.mtcfc_charset = cs;
            ASSERT(sic.sic_mtcfc.mtcfc_input_charset != NIL);
            ASSERT(sic.sic_mtcfc.mtcfc_charset != NIL);
         }
      }
#endif /* mx_HAVE_ICONV */

      if(sic.sic_mtcfc.mtcfc_conversion == CONV_8BIT)
         sctxp->sc_hp->h_flags |= HF_MESSAGE_8BITMIME;
   }

   /* Classify and readily iconv-prepare attachments */
   if(sctxp->sc_hp->h_attach != NIL){
      if(a_sendout__infix_attach(sctxp, &sic) != su_ERR_NONE)
         goto jerr;

      sic.sic_mime_boundary = mx_mime_param_boundary_create();
   }

   /* We have classified and charset-converted anything and are ready to go.
    * What is missing is the header, and its' classification and conversion.
    * Start creating our real output with infix_heads() using it already,
    * also creating readily prepared MIME-converted headers for all parts,
    * assuming' their meta data comes from same user context / locale / etc */

   /* Setup temporary file that holds readily prepared email. */
   sic.sic_encfp = mx_fs_tmp_open(NIL, "infix",
         (mx_FS_O_WRONLY | mx_FS_O_HOLDSIGS), &fstcp);
   if(sic.sic_encfp == NIL){
         sic.sic_emsg = N_("cannot create temporary mail file");
         goto jxerr;
   }

   sic.sic_encfp_input = mx_fs_open(fstcp->fstc_filename,
         n_real_good_or_bad(mx_FS_O_RDONLY, mx_FS_O_RDWR));
   if(sic.sic_encfp_input == NIL)
      sic.sic_eno = su_err();

   mx_fs_tmp_release(fstcp);

   if(sic.sic_encfp_input == NIL){
      sic.sic_emsg = N_("cannot open temporary mail file for reading");
      goto jerr;
   }

   if(a_sendout__infix_heads(sctxp, &sic) != su_ERR_NONE)
      goto jerr;

   if(a_sendout__infix_dump(sctxp, &sic) != su_ERR_NONE)
      goto jerr;

   /* We have readily created a converted message */
   rewind(sic.sic_encfp_input);
   mx_fs_close(sctxp->sc_input);
   sctxp->sc_input = sic.sic_encfp_input;
   sic.sic_encfp_input = NIL;

   ASSERT(sic.sic_eno == su_ERR_NONE);
jleave:
   if(sic.sic_encfp_input != NIL)
      mx_fs_close(sic.sic_encfp_input);
   if(sic.sic_encfp != NIL)
      mx_fs_close(sic.sic_encfp);

   n_pstate_err_no = sic.sic_eno;

   NYD_OU;
   return (sic.sic_eno == su_ERR_NONE);

jxerr:
   if((sic.sic_eno = su_err()) != su_ERR_IO)
      sic.sic_eno = su_ERR_INVAL;
jerr:/* C99 */{
   char const *cp;

   ASSERT(sic.sic_eno != su_ERR_NONE);
   cp = sic.sic_emsg;
   if(sic.sic_emsg_arg != NIL){
      if(cp == NIL)
         cp = N_("Cannot convert message via *sendcharsets*: %s: %s\n");
      n_err(V_(cp), sic.sic_emsg_arg, su_err_doc(sic.sic_eno));
   }else{
      if(cp == NIL)
         cp = N_("Cannot convert message via *sendcharsets*");
      n_perr(V_(cp), sic.sic_eno);
   }
   }goto jleave;
} /* }}} */

static s32
a_sendout__infix_attach(struct mx_send_ctx *sctxp, /* {{{ */
      struct a_sendout_infix_ctx *sicp){
   struct mx_attachment *ap;
   NYD_IN;

   for(ap = sctxp->sc_hp->h_attach; ap != NIL;
         ap->a_conv = mx_ATTACHMENTS_CONV_TMPFILE, ap = ap->a_flink){
      struct mx_mime_type_classify_fp_ctx *mtcfcp;
      FILE *ifp, *nfp;

      if(ap->a_conv == mx_ATTACHMENTS_CONV_TMPFILE)
         ifp = ap->a_tmpf;
      else if(ap->a_msgno){
/* FIXME these should be NOW not LAZY
 FIXME then also remove collect.c messages and so dealing with moving
 FIXME mailboxes when having msgno attachments */
         ap->a_mtcfc_or_nil = mtcfcp =
               su_AUTO_TCALLOC(struct mx_mime_type_classify_fp_ctx, 1);
         continue;
      }else if((ifp = mx_fs_open(ap->a_path, mx_FS_O_RDONLY)) != NIL)
         ap->a_tmpf = ifp;
      else{
         sicp->sic_eno = su_err();
         sicp->sic_emsg = N_("cannot open attachment file: %s: %s");
         sicp->sic_emsg_arg = ap->a_path;
         goto jleave;
      }

      /* */
      ap->a_mtcfc_or_nil = mtcfcp =
            su_AUTO_TCALLOC(struct mx_mime_type_classify_fp_ctx, 1);

      mx_mime_type_classify_fp_setup(mtcfcp, FAL0, ap->a_content_type, &sicp->sic_mpcc);
      if(!mx_mime_type_classify_fp(mtcfcp, ifp)){
#ifdef mx_HAVE_ICONV
jeclass:
#endif
         if((sicp->sic_eno = su_err()) == su_ERR_INVAL)
            sicp->sic_eno = su_ERR_IO;
         sicp->sic_emsg = N_("attachment MIME classification failed: %s: %s");
         sicp->sic_emsg_arg = ap->a_path;
         goto jleave;
      }
      if(ap->a_conv_force_b64)
         mtcfcp->mtcfc_conversion = CONV_TOB64;

      /* Unless iconv is not applicable / desired, try to convert to a desired
       * send character set, then re-classify the final content.
       * Otherwise restore user choices */
      if(ap->a_conv != mx_ATTACHMENTS_CONV_DEFAULT
#ifdef mx_HAVE_ICONV
            || !mtcfcp->mtcfc_do_iconv
#endif
            ){
         if(ap->a_charset != NIL)
            mtcfcp->mtcfc_charset = ap->a_charset;
         if(ap->a_input_charset_set)
            mtcfcp->mtcfc_input_charset = ap->a_input_charset;
         if(ap->a_conv == mx_ATTACHMENTS_CONV_FIX_INCS &&
               mtcfcp->mtcfc_ct_is_text_plain)
            mtcfcp->mtcfc_charset = mtcfcp->mtcfc_input_charset;
         if(mtcfcp->mtcfc_conversion == CONV_8BIT)
            sctxp->sc_hp->h_flags |= HF_MESSAGE_8BITMIME;
         continue;
      }

#ifdef mx_HAVE_ICONV
      nfp = NIL;
      if((sicp->sic_eno = mx_mime_charset_iter_onetime_fp(&nfp, ifp,
               mtcfcp, sctxp->sc_hp->h_charset, &sicp->sic_emsg)
            ) != su_ERR_NONE){
         ASSERT(nfp == NIL);
         sicp->sic_emsg_arg = ap->a_path;
         goto jleave;
      }

      /* If success came only due to *mime-force-sendout*, take "as-is"! */
      if(nfp != NIL){
         char const *ics, *cs;

         mx_fs_close(ifp);
         ap->a_tmpf = ifp = nfp;

         ics = mtcfcp->mtcfc_input_charset;
         cs = mtcfcp->mtcfc_charset;
         /* XXX We retry with original content-type after iconv'ersion here!? */
         mx_mime_type_classify_fp_setup(mtcfcp, FAL0, ap->a_content_type, &sicp->sic_mpcc);
         if(!mx_mime_type_classify_fp(mtcfcp, ifp))
            goto jeclass;
         mtcfcp->mtcfc_input_charset = ics;
         mtcfcp->mtcfc_charset = cs;
         ASSERT(mtcfcp->mtcfc_input_charset != NIL);
         ASSERT(mtcfcp->mtcfc_charset != NIL);
      }

      if(ap->a_conv_force_b64)
         mtcfcp->mtcfc_conversion = CONV_TOB64;
      else if(mtcfcp->mtcfc_conversion == CONV_8BIT)
         sctxp->sc_hp->h_flags |= HF_MESSAGE_8BITMIME;
#endif /* mx_HAVE_ICONV */
   }

jleave:
   NYD_OU;
   return sicp->sic_eno;
} /* }}} */

static s32
a_sendout__infix_heads(struct mx_send_ctx *sctxp, /* {{{ */
      struct a_sendout_infix_ctx *sicp){
   char const *hcs, *hcs_orig;
   boole need_cnv;
   struct mx_mime_probe_charset_ctx *mpccp;
   NYD_IN;

   mpccp = &sicp->sic_mpcc;
   need_cnv = mx_header_needs_mime(sctxp->sc_hp, &hcs, mpccp);
   hcs_orig = hcs;
   UNUSED(need_cnv);
   UNUSED(hcs_orig);

#ifdef mx_HAVE_ICONV
   if(iconvd != R(iconv_t,-1)) /* XXX anyway for mime.c++ :-( */
      n_iconv_close(iconvd);

   if(mpccp->mpcc_iconv_disable){
      if(need_cnv > FAL0)
#endif
      {
         /* TODO v15-compat hack: until rewrite, mime.c etc does not have any
          * TODO glue of mime_probe_charset_ctx but simply uses variables:
          * TODO therefore inject mime_probe result at the front of the
          * TODO mime_charset_iter! */
         mx_mime_charset_iter_reset(hcs, NIL);
         ASSERT(mx_mime_charset_iter_is_valid());
      }
      need_cnv = FAL0;
#ifdef mx_HAVE_ICONV
   }else if(need_cnv > FAL0){
      ASSERT(hcs != NIL);
      mx_mime_charset_iter_reset(sctxp->sc_hp->h_charset, hcs);
      ASSERT(mx_mime_charset_iter_is_valid());

      if(0){
j_redo:
         rewind(sicp->sic_encfp);
         ftrunc_x_trunc(sicp->sic_encfp, 0, sicp->sic_eno);
         if(sicp->sic_eno != 0){
            if((sicp->sic_eno = su_err_by_errno()) == su_ERR_NONE)
               sicp->sic_eno = su_ERR_IO;
            goto jleave;
         }
      }

      /* Do not avoid things like utf-8 -> utf-8 to be able to detect encoding
       * errors XXX also this should be !iconv_is_same_charset(), and THAT.. */
      if(need_cnv > FAL0 && /*su_cs_cmp_case(convhdr, tcs) != 0 &&*/
            (iconvd = n_iconv_open(mx_mime_charset_iter(), hcs)
                  ) == R(iconv_t,-1)){
jiter:
         if(need_cnv > FAL0){
            if(mx_mime_charset_iter_next())
               goto j_redo;
            if(ok_blook(mime_force_sendout)){
               need_cnv = TRUM1;
               hcs = hcs_orig;
               goto j_redo;
            }
            sicp->sic_eno = su_ERR_INVAL; /* xxx NOTSUP */
            goto jleave;
         }
      }
   }
#endif /* mx_HAVE_ICONV */

   /* */
   n_pstate &= ~n_PS_HEADER_NEEDED_MIME; /* TODO hack -> be carrier tracked */
   if(!n_puthead(SEND_MBOX, FAL0, sicp->sic_encfp, sctxp->sc_hp,
         (GTO | GSUBJECT | GCC | GBCC | GCOMMA | GUA | GMSGID |
          GIDENT | GREF | GDATE))){
#ifdef mx_HAVE_ICONV
      if((sicp->sic_eno = n_iconv_err) == su_ERR_ILSEQ ||
            sicp->sic_eno == su_ERR_NOENT || sicp->sic_eno == su_ERR_INVAL)
         goto jiter;
#endif
      if((sicp->sic_eno = su_err()) == su_ERR_NONE)
         sicp->sic_eno = su_ERR_INVAL;
      goto jleave;
   }

   /* If we have attachments, prepare their headers with the very same
    * character set that succeeded for the main header!
    * TODO Because of MIME layer restrictions we need to dump that to a file,
    * TODO and move over the attachment MIME headers to memory thereafter,
    * TODO until the attachment is then dumped; added mtcfc_data.* for this! */
   if(sctxp->sc_hp->h_attach != NIL){
      s64 off, len;
      struct mx_attachment *ap;

      ftrunc_x_fflush_tell(sicp->sic_encfp, off);
      if(off == -1){
         if((sicp->sic_eno = su_err_by_errno()) == su_ERR_NONE)
            sicp->sic_eno = su_ERR_IO;
         goto jleave;
      }

      for(ap = sctxp->sc_hp->h_attach; ap != NIL; ap = ap->a_flink){
         /* Content-ID: in _dump() */
         if(!ap->a_msgno){
            if(a_sendout__infix_cd(sicp->sic_encfp, ap->a_content_disposition,
                     ap->a_name, mpccp) < 0 ||
                  a_sendout__infix_ct(sicp->sic_encfp, ap->a_mtcfc_or_nil
                     ) < 0 ||
                  a_sendout__infix_cte(sicp->sic_encfp, ap->a_mtcfc_or_nil
                     ) < 0){
               sicp->sic_eno = su_err();
#ifdef mx_HAVE_ICONV
               if(sicp->sic_eno == su_ERR_INVAL)
                  goto jiter;
#endif
               ASSERT(sicp->sic_eno != su_ERR_NONE);
               goto jleave;
            }
         }

         if(ap->a_content_description != NIL){
            if(fputs("Content-Description: ", sicp->sic_encfp) == EOF){
               sicp->sic_eno = su_err_by_errno();
               goto jleave;
            }
            if(mx_xmime_write(ap->a_content_description,
                  su_cs_len(ap->a_content_description), sicp->sic_encfp,
                  CONV_TOHDR, (mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT),
                  NIL, NIL) < 0){
               sicp->sic_eno = su_err();
#ifdef mx_HAVE_ICONV
               if(sicp->sic_eno == su_ERR_INVAL)
                  goto jiter;
#endif
               ASSERT(sicp->sic_eno != su_ERR_NONE);
               goto jleave;
            }
            if(putc('\n', sicp->sic_encfp) == EOF){
               sicp->sic_eno = su_err_by_errno();
               goto jleave;
            }
         }

         ftrunc_x_fflush_tell(sicp->sic_encfp, len);

         if(len == -1){
            if((sicp->sic_eno = su_err_by_errno()) == su_ERR_NONE)
               sicp->sic_eno = su_ERR_IO;
            goto jleave;
         }

         if(off > S32_MAX || len > S32_MAX){ /* TODO no 64-bit I/O stuff */
            sicp->sic_eno = su_ERR_MSGSIZE;
            goto jleave;
         }
         len -= off;

         /* I first wanted to loop over all, then on success once again and
          * store once; but even in the worst case these should not exceed
          * a few kilobytes, and so this now seems good enough v15-compat */
         if((ap->a_mtcfc_or_nil->mtcfc_data.l = S(uz,len)) == 0)
            continue;

         ap->a_mtcfc_or_nil->mtcfc_data.s = su_AUTO_ALLOC(
               ap->a_mtcfc_or_nil->mtcfc_data.l); /* (no NUL) */

         if(!n_real_seek(sicp->sic_encfp_input, off, SEEK_SET)/* XXX std! */ ||
               fread(ap->a_mtcfc_or_nil->mtcfc_data.s,
                  sizeof(*ap->a_mtcfc_or_nil->mtcfc_data.s),
                  ap->a_mtcfc_or_nil->mtcfc_data.l, sicp->sic_encfp_input
               ) != ap->a_mtcfc_or_nil->mtcfc_data.l){
            if((sicp->sic_eno = su_err_by_errno()) == su_ERR_NONE)
               sicp->sic_eno = su_ERR_IO;
            goto jleave;
         }

         if(fseek(sicp->sic_encfp, off, SEEK_SET) == -1){
            if((sicp->sic_eno = su_err_by_errno()) == su_ERR_NONE)
               sicp->sic_eno = su_ERR_IO;
            goto jleave;
         }
      }

      /* v15-compat TODO totally trunc away our temporary headers */
      ftrunc_x_trunc(sicp->sic_encfp, off, len);
      if(len != 0){
         if((sicp->sic_eno = su_err_by_errno()) == su_ERR_NONE)
            sicp->sic_eno = su_ERR_IO;
         goto jleave;
      }
   }

   /* We were able to create all message headers */
jleave:
   NYD_OU;
   return sicp->sic_eno;
} /* }}} */

static s32
a_sendout__infix_dump(struct mx_send_ctx *sctxp, /* {{{ */
      struct a_sendout_infix_ctx *sicp){
   struct mx_mime_type_classify_fp_ctx *mtcfcp;
   NYD_IN;
   ASSERT(sicp->sic_eno == su_ERR_NONE);

   mtcfcp = &sicp->sic_mtcfc;

   /* Finalize main header block.
    *
    * RFC 5322 says:
    *    2.1.  General Description
    *    At the most basic level, a message is a series of characters.  A
    *    message that is conformant with this specification is composed of
    *    characters with values in the range of 1 through 127 and interpreted
    *    as US-ASCII [ANSI.X3-4.1986] characters.  For brevity, this document
    *    sometimes refers to this range of characters as simply "US-ASCII
    *    characters".
    *
    * And thus we place MIME only when needed despite RFC 2045 says
    *
    *    4.  MIME-Version Header Field
    *    [.]In the absence of a MIME-Version field, a receiving mail user agent
    *    (whether conforming to MIME requirements or not) may optionally
    *    choose to interpret the body of the message according to local
    *    conventions.  Many such conventions are currently in use and it
    *    should be noted that in practice non-MIME messages can contain just
    *    about anything.
    *
    *    It is impossible to be certain that a non-MIME mail message is
    *    actually plain text in the US-ASCII character set[.]
    *
    * Let aside it also says
    *
    *    5.2.  Content-Type Defaults
    *    [.]Plain US-ASCII text may still be assumed in the absence of a
    *    MIME-Version or the presence of an syntactically invalid Content-Type
    *    header field, but the sender's intent might have been otherwise.
    *
    * TODO RFC 2046 specifies that the same Content-ID should be used
    * TODO for identical data; this is too hard for us right now,
    * TODO because if done right it should be checksum based!?! */
   if((n_pstate & n_PS_HEADER_NEEDED_MIME) || sctxp->sc_hp->h_attach != NIL ||
         (!sicp->sic_no_body &&
            (mtcfcp->mtcfc_conversion != CONV_7BIT ||
             !mtcfcp->mtcfc_charset_is_ascii))){
      if(fputs("MIME-Version: 1.0\n", sicp->sic_encfp) == EOF)
         goto jerr;

      if(sctxp->sc_hp->h_attach != NIL){
         if(fputs("Content-Type: multipart/mixed;", sicp->sic_encfp) == EOF ||
               ((su_cs_len(sicp->sic_mime_boundary) > 37)
                  ? (putc('\n', sicp->sic_encfp) == EOF) : FAL0) ||
               putc(' ', sicp->sic_encfp) == EOF ||
               fputs("boundary=\"", sicp->sic_encfp) == EOF ||
               fputs(sicp->sic_mime_boundary, sicp->sic_encfp) == EOF ||
               fputs("\"\n\nThis is a multi-part message in MIME format.\n",
                  sicp->sic_encfp) == EOF)
            goto jerr;

         if(!sicp->sic_no_body){
            /* (initial \n debatable) */
            if(fputs("\n--", sicp->sic_encfp) == EOF ||
                  fputs(sicp->sic_mime_boundary, sicp->sic_encfp) == EOF ||
                  fputs("\nContent-Disposition: inline\n", sicp->sic_encfp
                     ) == EOF)
               goto jerr;

            if(sicp->sic_gen_ids){
               char const *cp;

               if((cp = a_sendout_random_id(sctxp->sc_hp, FAL0)) != NIL &&
                     fprintf(sicp->sic_encfp, "Content-ID: <%s>\n", cp) < 0)
                  goto jerr;
            }
         }
      }

      if(!sicp->sic_no_body){
         if(a_sendout__infix_ct(sicp->sic_encfp, mtcfcp) < 0 ||
               a_sendout__infix_cte(sicp->sic_encfp, mtcfcp) < 0){
            sicp->sic_eno = su_err();
            goto jenono;
         }
      }
   }

   if(putc('\n', sicp->sic_encfp) == EOF){
      if((sicp->sic_eno = su_err_by_errno()) == su_ERR_NONE)
         sicp->sic_eno = su_ERR_IO;
      goto jenono;
   }

   if(!sicp->sic_no_body){
      if((sicp->sic_eno = a_sendout__infix_file(mtcfcp->mtcfc_conversion,
               sicp->sic_encfp, sctxp->sc_input)) != su_ERR_NONE)
         goto jenono;
   }

   if(sctxp->sc_hp->h_attach != NIL){
      struct mx_attachment *ap;

      for(ap = sctxp->sc_hp->h_attach; ap != NIL; ap = ap->a_flink){
         char const *cp;

         /* TODO RFC 2046 says:
          * TODO  NOTE:  The CRLF preceding the boundary delimiter line is
          * TODO  conceptually attached to the boundary so that it is possible
          * TODO  to have a part that does not end with a CRLF (line  break).
          * TODO  Body parts that must be considered to end with line breaks,
          * TODO  therefore, must have two CRLFs preceding the boundary
          * TODO  delimiter line, the first of which is part of the preceding
          * TODO  body part, and the second of which is part of the
          * TODO  encapsulation boundary,
          * TODO Unfortunately this software uses the very same logic to deal
          * TODO with RFC 5322 5321 4155, meaning that IF we would adhere to
          * TODO this a `write' of such a MIME part ends up without trailing
          * TODO newline, for example.
          * TODO So, before v15-compat, we consciously do it wrong in order to
          * TODO end up with something we can deal with. */
         if((!sicp->sic_no_body || ap != sctxp->sc_hp->h_attach) &&
               putc('\n', sicp->sic_encfp) == EOF)
            goto jerr;

         if(fputs("--", sicp->sic_encfp) == EOF ||
               fputs(sicp->sic_mime_boundary, sicp->sic_encfp) == EOF ||
               putc('\n', sicp->sic_encfp) == EOF)
            goto jerr;

         /* TODO no msgno handling, should be TMPFILE already! */
         if(ap->a_msgno &&
               fputs(
                  "Content-Disposition: inline\nContent-Type: message/rfc822\n",
                  sicp->sic_encfp) == EOF)
            goto jerr;

         if(ap->a_mtcfc_or_nil->mtcfc_data.l > 0 &&
               fwrite(ap->a_mtcfc_or_nil->mtcfc_data.s,
                  sizeof(*ap->a_mtcfc_or_nil->mtcfc_data.s),
                  ap->a_mtcfc_or_nil->mtcfc_data.l, sicp->sic_encfp
               ) != ap->a_mtcfc_or_nil->mtcfc_data.l)
            goto jerr;

         if(sicp->sic_gen_ids){
            if(ap->a_content_id != NIL)
               cp = ap->a_content_id->n_name;
            else
               cp = a_sendout_random_id(sctxp->sc_hp, FAL0);
            if(cp != NIL &&
                  fprintf(sicp->sic_encfp, "Content-ID: <%s>\n", cp) < 0)
               goto jerr;
         }

         if(putc('\n', sicp->sic_encfp) == EOF)
            goto jerr;

         if(ap->a_msgno){
            struct message *mp;

            mp = &message[ap->a_msgno - 1]; /* TODO early init to tmpfile! */
            touch(mp);

            if(sendmp(mp, sicp->sic_encfp, 0, NIL, SEND_RFC822, NIL, NIL) < 0){
               if((sicp->sic_eno = su_err()) == su_ERR_NONE)
                  sicp->sic_eno = su_ERR_IO;
               goto jenono;
            }
         }else if((sicp->sic_eno = a_sendout__infix_file(
                  ap->a_mtcfc_or_nil->mtcfc_conversion,
                  sicp->sic_encfp, ap->a_tmpf)) != su_ERR_NONE)
            goto jenono;
      }

      /* TODO Again, as above! */
      if(putc('\n', sicp->sic_encfp) == EOF)
         goto jerr;
      if(fputs("--", sicp->sic_encfp) == EOF ||
            fputs(sicp->sic_mime_boundary, sicp->sic_encfp) == EOF ||
            fputs("--\n", sicp->sic_encfp) == EOF)
         goto jerr;
   }

   if(fflush(sicp->sic_encfp) == EOF)
      goto jerr;

jleave:
   NYD_OU;
   return sicp->sic_eno;

jerr:
   if((sicp->sic_eno = su_err_by_errno()) == su_ERR_NONE)
      sicp->sic_eno = su_ERR_IO;
jenono:
   sicp->sic_emsg = N_("I/O error occurred while creating mail message");
   goto jleave;
} /* }}} */

static s32
a_sendout__infix_file(enum conversion conv, FILE *ofp, FILE *ifp){ /* {{{ */
   struct str outrest, inrest;
   boole easy, iseof, seenempty;
   char *buf;
   uz size, bufsize, cnt;
   s32 rv;
   NYD2_IN;
   ASSERT(ftell(ifp) == 0);

   mx_fs_linepool_aquire(&buf, &bufsize);
   outrest.s = inrest.s = NIL;
   outrest.l = inrest.l = 0;

   easy = (conv != CONV_TOQP && conv != CONV_8BIT && conv != CONV_7BIT);
   if(!easy)
      cnt = fsize(ifp);

   seenempty = iseof = FAL0;
   while(!iseof){
      if(!easy){
         if(fgetline(&buf, &bufsize, &cnt, &size, ifp, FAL0) == NIL)
            break;
         if(conv == CONV_TOQP)
            seenempty = (size == 1 /*&& buf[0] == '\n'*/);
         else{
            if(seenempty && is_head(buf, size, FAL0)){
               if(putc('>', ofp) == EOF){
                  rv = su_err_by_errno();
                  goto jleave;
               }
               seenempty = FAL0;
            }else
               seenempty = (size == 1 /*&& buf[0] == '\n'*/);

            /* Thanks to classification we can simply write through! */
            if(fwrite(buf, sizeof *buf, size, ofp) != size){
               rv = su_err_by_errno();
               goto jleave;
            }
            continue;
         }
      }else if((size = fread(buf, sizeof *buf, bufsize, ifp)) == 0)
         break;

      /* TODO QP and base64 could now also be written *here* via mime-enc
       * TODO which would be much faster! */
joutln:
      if(mx_xmime_write(buf, size, ofp, conv, mx_MIME_DISPLAY_NONE, &outrest,
               (iseof > FAL0 ? NIL : &inrest)) < 0){
         rv = su_err();
         goto jleave;
      }
   }
   if(iseof <= FAL0 && (outrest.l != 0 || inrest.l != 0)){
      size = 0;
      iseof = (iseof || inrest.l == 0) ? TRU1 : TRUM1;
      goto joutln;
   }

   rv = ferror(ifp) ? su_ERR_IO : su_ERR_NONE;
jleave:
   if(outrest.s != NIL)
      su_FREE(outrest.s);
   if(inrest.s != NIL)
      su_FREE(inrest.s);
   mx_fs_linepool_release(buf, bufsize);

   NYD_OU;
   return rv;
} /* }}} */

static int
a_sendout__infix_ct(FILE *ofp, /* {{{ */
      struct mx_mime_type_classify_fp_ctx *mtcfcp){
   int rv;
   NYD2_IN;

   /* RFC 2045, 5.2.  Content-Type Defaults:
    *    Default RFC 822 messages without a MIME Content-Type header are taken
    *    by this protocol to be plain text in the US-ASCII character set,
    *    which can be explicitly specified as:
    *       Content-type: text/plain; charset=us-ascii */
#if 0
XXX explicit is better, and i am in fear of some version of some software not
XXX liking that: make it an option??
   if(mtcfcp->mtcfc_charset_is_ascii && mtcfcp->mtcfc_ct_is_text_plain){
      rv = 0;
      goto jleave;
   }
#endif

   if((rv = fprintf(ofp, "Content-Type: %s", mtcfcp->mtcfc_content_type)) < 0)
      goto jerr;

   if(mtcfcp->mtcfc_charset != NIL){
      struct str s;

      if(mx_mime_param_create(TRU1, &s, "charset", mtcfcp->mtcfc_charset,
            mtcfcp->mtcfc_mpccp_or_nil) <= FAL0){
         su_err_set(su_ERR_INVAL);
         rv = -1;
         goto jleave;
      }

      if(putc(';', ofp) == EOF)
         goto jerr;
      ++rv;

      /* It fits in MIME_LINELEN_LIMIT anyhow! */
      if(s.l + rv >= MIME_LINELEN){
         ++rv;
         if(putc('\n', ofp) == EOF)
            goto jerr;
      }

      if(putc(' ', ofp) == EOF || fputs(s.s, ofp) == EOF)
         goto jerr;

      rv += 2 + S(int,s.l);
   }

   if(putc('\n', ofp) != EOF)
      ++rv;
   else{
jerr:
      su_err_by_errno();
      rv = -1;
   }

jleave:
   NYD2_OU;
   return rv;
} /* }}} */

static int
a_sendout__infix_cte(FILE *ofp, struct mx_mime_type_classify_fp_ctx *mtcfcp){
   int rv;
   NYD2_IN;

   /* RFC 2045, 6.1.:
    *    This is the default value -- that is,
    *    "Content-Transfer-Encoding: 7BIT" is assumed if the
    *     Content-Transfer-Encoding header field is not present. */
   if(mtcfcp->mtcfc_conversion == CONV_7BIT)
      rv = 0;
   else{
      rv = fprintf(ofp, "Content-Transfer-Encoding: %s\n",
            mx_mime_enc_name_from_conversion(mtcfcp->mtcfc_conversion));
      if(rv < 0)
         su_err_by_errno();
   }

   NYD2_OU;
   return rv;
}

static int
a_sendout__infix_cd(FILE *ofp, char const *cd, char const *filename, /* {{{ */
      struct mx_mime_probe_charset_ctx const *mpccp){
   struct str f;
   s8 mpc;
   int rv;
   NYD2_IN;

   f.s = NIL;

   /* xxx Ugly with the trailing space in case of wrap! */
   rv = fprintf(ofp, "Content-Disposition: %s; ", cd);
   if(rv < 0)
      goto jerr;

   mpc = mx_mime_param_create(FAL0, &f, "filename", filename, mpccp);
   if(!mpc){
      su_err_set(su_ERR_INVAL);
      rv = -1;
      goto jleave;
   }
   /* Always fold if result contains newlines */
   if(mpc < 0 || f.l + rv > MIME_LINELEN /* XXX always <MIME_LINELEN_MAX */){
      if(putc('\n', ofp) == EOF || putc(' ', ofp) == EOF)
         goto jerr;
      rv += 2;
   }
   if(fputs(f.s, ofp) != EOF && putc('\n', ofp) != EOF)
      rv += S(int,++f.l);
   else{
jerr:
      su_err_by_errno();
      rv = -1;
   }

jleave:
   NYD2_OU;
   return rv;
} /* }}} */

static boole
_check_dispo_notif(struct mx_name *mdn, struct header *hp, FILE *fo)
{
   char const *from;
   boole rv = TRU1;
   NYD_IN;

   /* TODO smtp_disposition_notification (RFC 3798): relation to return-path
    * TODO not yet checked */
   if (!ok_blook(disposition_notification_send))
      goto jleave;

   if (mdn != NULL && mdn != (struct mx_name*)0x1)
      from = mdn->n_name;
   else if ((from = myorigin(hp)) == NULL) {
      if (n_poption & n_PO_D_V)
         n_err(_("*disposition-notification-send*: *from* not set\n"));
      goto jleave;
   }

   if (!a_sendout_put_addrline("Disposition-Notification-To:",
         nalloc(n_UNCONST(from), 0), fo, 0))
      rv = FAL0;
jleave:
   NYD_OU;
   return rv;
}

static int
a_sendout_sendmail(void *vp, enum n_mailsend_flags msf){
   struct header head;
   int rv;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   STRUCT_ZERO(struct header, &head);
   head.h_flags = HF_CMD_mail;
   cacp = vp;
   if(cacp->cac_no > 0 &&
         (head.h_to = lextract(cacp->cac_arg->ca_arg.ca_str.s,
            (GTO | (ok_blook(fullnames) ? GFULL | GSKIN : GSKIN)))) != NIL)
      head.h_mailx_raw_to = n_namelist_dup(head.h_to, head.h_to->n_type);

   rv = n_mail1(msf, cacp->cac_scope, &head, NIL, NIL);

   NYD_OU;
   return (rv != OKAY); /* reverse! */
}

static struct mx_name *
a_sendout_file_a_pipe(struct mx_name *names, FILE *fo, boole *senderror){/*{{{*/
   boole mfap;
   u32 pipecnt, xcnt, i, swf;
   struct mx_name *np;
   FILE *fp, **fppa;
   NYD_IN;

   fp = NIL;
   fppa = NIL;

   /* Look through all recipients and do a quick return if no file or pipe
    * addressee is found */
   for(pipecnt = xcnt = 0, np = names; np != NIL; np = np->n_flink){
      if(np->n_type & GDEL)
         continue;
      switch(np->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE){
      case mx_NAME_ADDRSPEC_ISFILE: ++xcnt; break;
      case mx_NAME_ADDRSPEC_ISPIPE: ++pipecnt; break;
      }
   }
   if((pipecnt | xcnt) == 0)
      goto jleave;

   /* Otherwise create an array of file descriptors for each found pipe
    * addressee to get around the dup(2)-shared-file-offset problem, i.e.,
    * each pipe subprocess needs its very own file descriptor, and we need
    * to deal with that.
    * This is true even if *sendwait* requires fully synchronous mode, since
    * the shell handlers can fork away and pass the descriptor around, so we
    * cannot simply use a single one and rewind that after the top children
    * shell has returned.
    * To make our life a bit easier let us just use the auto-reclaimed
    * string storage */
   if(pipecnt == 0 || (n_poption & n_PO_D))
      pipecnt = 0;
   else{
      i = sizeof(FILE*) * pipecnt;
      fppa = su_LOFI_ALLOC(i);
      su_mem_set(fppa, 0, i);
   }

   mfap = ok_blook(mbox_fcc_and_pcc);
   swf = a_sendout_sendwait_to_swf();

   for(np = names; np != NIL; np = np->n_flink){
      if(!(np->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE) || (np->n_type & GDEL))
         continue;

      /* In days of old we removed the entry from the the list; now for sake of
       * header expansion we leave it in and mark it as deleted */
      np->n_type |= GDEL;

      if(n_poption & n_PO_D_VV)
         n_err(_(">>> Writing message via %s\n"),
            n_shexp_quote_cp(np->n_name, FAL0));
      /* We _do_ write to STDOUT, anyway! */
      if((n_poption & n_PO_D) &&
            ((np->n_flags & mx_NAME_ADDRSPEC_ISPIPE) ||
               np->n_name[0] != '-' || np->n_name[1] != '\0'))
         continue;

      /* See if we have copied the complete message out yet.  If not, do so */
      if(fp == NIL){
         int c;
         struct mx_fs_tmp_ctx *fstcp;

         if((fp = mx_fs_tmp_open(NIL, "outof", (mx_FS_O_RDWR |
                  mx_FS_O_HOLDSIGS), &fstcp)) == NIL){
            n_perr(_("Creation of temporary image"), 0);
            pipecnt = 0;
            goto jerror;
         }

         for(i = 0; i < pipecnt; ++i)
            if((fppa[i] = mx_fs_open(fstcp->fstc_filename, mx_FS_O_RDONLY)
                  ) == NIL){
               n_perr(_("Creation of pipe image descriptor"), 0);
               break;
            }

         mx_fs_tmp_release(fstcp);

         if(i != pipecnt){
            pipecnt = i;
            goto jerror;
         }

         if(mfap)
            fprintf(fp, "From %s %s",
               ok_vlook(LOGNAME), mx_time_current.tc_ctime);
         c = EOF;
         while(i = c, (c = getc(fo)) != EOF)
            putc(c, fp);
         rewind(fo);
         if((int)i != '\n')
            putc('\n', fp);
         if(mfap)
            putc('\n', fp);
         fflush(fp);
         if(ferror(fp)){
            n_perr(_("Finalizing write of temporary image"),
                  su_err_by_errno());
            goto jerror;
         }

         /* From now on use xcnt as a counter for pipecnt */
         xcnt = 0;
      }

      /* Now either copy "image" to the desired file or give it as the
       * standard input to the desired program as appropriate */
      if(np->n_flags & mx_NAME_ADDRSPEC_ISPIPE){
         struct mx_child_ctx cc;
         sigset_t nset;

         sigemptyset(&nset);
         sigaddset(&nset, SIGHUP);
         sigaddset(&nset, SIGINT);
         sigaddset(&nset, SIGQUIT);

         mx_child_ctx_setup(&cc);
         cc.cc_flags = mx_CHILD_SPAWN_CONTROL |
               (swf & a_SENDOUT_SWF_PCC ? mx_CHILD_RUN_WAIT_LIFE : 0);
         cc.cc_mask = &nset;
         cc.cc_fds[mx_CHILD_FD_IN] = fileno(fppa[xcnt]);
         cc.cc_fds[mx_CHILD_FD_OUT] = mx_CHILD_FD_NULL;
         mx_child_ctx_set_args_for_sh(&cc, NIL, &np->n_name[1]);

         if(!mx_child_run(&cc)){
            n_err(_("Piping message to %s failed\n"),
               n_shexp_quote_cp(np->n_name, FAL0));
            goto jerror;
         }

         /* C99 */{
            FILE *tmp;

            tmp = fppa[xcnt];
            fppa[xcnt++] = NIL;
            mx_fs_close(tmp);
         }

         if(!(swf & a_SENDOUT_SWF_PCC))
            mx_child_forget(&cc);
      }else{
         int c;
         FILE *fout;
         char const *fname, *fnameq;

         if((fname = mx_fexpand(np->n_name, mx_FEXP_DEF_FOLDER_VAR)) == NIL)/* TODO */
            goto jerror;
         fnameq = n_shexp_quote_cp(fname, FAL0);

         if(fname[0] == '-' && fname[1] == '\0')
            fout = n_stdout;
         else{
            int xerr;
            BITENUM(u32,mx_fs_open_state) fs;

            if((fout = mx_fs_open_any(fname, (mx_FS_O_CREATE |
                     (mfap ? mx_FS_O_RDWR | mx_FS_O_APPEND
                      : mx_FS_O_WRONLY | mx_FS_O_TRUNC)), &fs)) == NIL){
jefileeno:
               xerr = su_err();
jefile:
               n_err(_("Writing message to %s failed: %s\n"),
                  fnameq, su_err_doc(xerr));
               goto jerror;
            }

            if((fs & (n_PROTO_MASK | mx_FS_OPEN_STATE_EXISTS)) ==
                  (n_PROTO_FILE | mx_FS_OPEN_STATE_EXISTS)){
               if(!mx_file_lock(fileno(fout), (mx_FILE_LOCK_MODE_TEXCL |
                     mx_FILE_LOCK_MODE_RETRY)))
                  goto jefileeno;
               if(mfap && (xerr = n_folder_mbox_prepare_append(fout, FAL0, NIL)
                     ) != su_ERR_NONE)
                  goto jefile;
            }
         }

         rewind(fp);
         while((c = getc(fp)) != EOF)
            putc(c, fout);
         if(ferror(fout)){
            n_err(_("Writing message to %s failed: %s\n"),
               fnameq, _("write error"));
            *senderror = TRU1;
         }

         if(fout != n_stdout)
            mx_fs_close(fout);
         else
            clearerr(fout);
      }
   }

jleave:
   if(fp != NIL)
      mx_fs_close(fp);

   if(fppa != NIL){
      for(i = 0; i < pipecnt; ++i)
         if((fp = fppa[i]) != NIL)
            mx_fs_close(fp);
      su_LOFI_FREE(fppa);
   }

   NYD_OU;
   return names;

jerror:
   *senderror = TRU1;
   while(np != NIL){
      if(np->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE)
         np->n_type |= GDEL;
      np = np->n_flink;
   }
   goto jleave;
} /* }}} */

static boole
a_sendout_mightrecord(FILE *fp, struct mx_name *to, boole resend){ /* {{{ */
   char const *ccp;
   char *cp;
   boole rv;
   NYD2_IN;

   rv = TRU1;

   if(to != NIL){
      ccp = cp = savestr(to->n_name);
      while(*cp != '\0' && *cp != '@')
         ++cp;
      *cp = '\0';
   }else
      ccp = ok_vlook(record);

   if(ccp == NIL)
      goto jleave;

   if((cp = mx_fexpand(ccp, mx_FEXP_DEF_FOLDER_VAR)) == NIL)
      goto jbail;

   switch(which_protocol(ccp, FAL0, FAL0, NIL)){
   case n_PROTO_EML:
   case n_PROTO_POP3:
   case n_PROTO_UNKNOWN:
      goto jbail;
   default:
      break;
   }

   switch(*(ccp = cp)){
   case '.':
      if(cp[1] != '/'){ /* DIRSEP */
   default:
         if(ok_blook(outfolder)){
            struct str s;
            char const *nccp, *folder;

            switch(which_protocol(ccp, TRU1, FAL0, &nccp)){
            case PROTO_FILE:
               ccp = "file://";
               if(0){
               /* FALLTHRU */
            case PROTO_MAILDIR:
#ifdef mx_HAVE_MAILDIR
                  ccp = "maildir://";
#else
                  n_err(_("*record*: *outfolder*: no Maildir directory "
                     "support compiled in\n"));
                  goto jbail;
#endif
               }
               folder = n_folder_query();
#ifdef mx_HAVE_IMAP
               if(which_protocol(folder, FAL0, FAL0, NIL) == PROTO_IMAP){
                  n_err(_("*record*: *outfolder* set, *folder* is IMAP "
                     "based: only one protocol per file is possible\n"));
                  goto jbail;
               }
#endif
               ccp = str_concat_csvl(&s, ccp, folder, nccp, NIL)->s;
               /* FALLTHRU */
            case n_PROTO_IMAP:
               break;
            default:
               goto jbail;
            }
         }
      }
      /* FALLTHRU */
   case '/':
      break;
   }

   if(n_poption & n_PO_D_VV)
      n_err(_(">>> Writing message via %s\n"), n_shexp_quote_cp(ccp, FAL0));

   if(!(n_poption & n_PO_D) && !a_sendout__savemail(ccp, fp, resend)){
jbail:
      n_err(_("Failed to save message in %s - message not sent\n"),
         n_shexp_quote_cp(ccp, FAL0));
      n_exit_status |= su_EX_ERR;
      savedeadletter(fp, 1);
      rv = FAL0;
   }

jleave:
   NYD2_OU;
   return rv;
} /* }}} */

static boole
a_sendout__savemail(char const *name, FILE *fp, boole resend){ /* {{{ */
   FILE *fo;
   uz bufsize, buflen, cnt;
   BITENUM(u32,mx_fs_open_state) fs;
   char *buf;
   boole rv, emptyline;
   NYD_IN;
   UNUSED(resend);

   rv = FAL0;
   mx_fs_linepool_aquire(&buf, &bufsize);

   if((fo = mx_fs_open_any(name, (mx_FS_O_RDWR | mx_FS_O_APPEND |
            mx_FS_O_CREATE), &fs)) == NIL){
      n_perr(name, 0);
      goto j_leave;
   }

   if((fs & (n_PROTO_MASK | mx_FS_OPEN_STATE_EXISTS)) ==
         (n_PROTO_FILE | mx_FS_OPEN_STATE_EXISTS)){
      int xerr;

      /* TODO RETURN check, but be aware of protocols: v15: Mailbox->lock()!
       * TODO BETTER yet: should be returned in lock state already! */
      if(!mx_file_lock(fileno(fo), (mx_FILE_LOCK_MODE_TEXCL |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
         xerr = su_err();
         goto jeappend;
      }

      if((xerr = n_folder_mbox_prepare_append(fo, FAL0, NIL)) != su_ERR_NONE){
jeappend:
         n_perr(name, xerr);
         goto jleave;
      }
   }

   if(fprintf(fo, "From %s %s", ok_vlook(LOGNAME), mx_time_current.tc_ctime) < 0)
      goto jleave;

   rv = TRU1;

   fflush_rewind(fp);
   for(emptyline = FAL0, buflen = 0, cnt = fsize(fp);
         fgetline(&buf, &bufsize, &cnt, &buflen, fp, FAL0) != NIL;){
      /* Only if we are resending it can happen that we have to quote From_
       * lines here; we don't generate messages which are ambiguous ourselves.
       * xxx No longer true after (Reintroduce ^From_ MBOXO with
       * xxx *mime-encoding*=8b (too many!)[..]) */
      /*if(resend){*/
         if(emptyline && is_head(buf, buflen, FAL0)){
            if(putc('>', fo) == EOF){
               rv = FAL0;
               break;
            }
         }
      /*}DVLDBG(else ASSERT(!is_head(buf, buflen, FAL0)); )*/

      emptyline = (buflen > 0 && *buf == '\n');
      if(fwrite(buf, sizeof *buf, buflen, fo) != buflen){
         rv = FAL0;
         break;
      }
   }
   if(rv){
      if(buflen > 0 && buf[buflen - 1] != '\n'){
         if(putc('\n', fo) == EOF)
            rv = FAL0;
      }
      if(rv && (putc('\n', fo) == EOF || fflush(fo)))
         rv = FAL0;
   }

   if(!rv){
      n_perr(name, su_ERR_IO);
      rv = FAL0;
   }

jleave:
   /* C99 */{
      int e;

      really_rewind(fp, e);
      if(e)
         rv = FAL0;
   }
   if(!mx_fs_close(fo))
      rv = FAL0;

j_leave:
   mx_fs_linepool_release(buf, bufsize);

   NYD_OU;
   return rv;
} /* }}} */

static boole
a_sendout_transfer(struct mx_send_ctx *scp, boole resent, /* {{{ */
      boole *senderror){
   u32 cnt;
   struct mx_name *np;
   FILE *input_save;
   boole rv;
   NYD_IN;

   rv = FAL0;

   /* Do we need to create a Bcc: free overlay?
    * Only for file-based non-test:// MTAs.
    * TODO In v15 we would have an object tree with dump-to-wire, we have our
    * TODO file stream which acts upon an I/O device that stores so-and-so-much
    * TODO memory, excess in a temporary file; either each object knows its
    * TODO offset where it placed its dump-to-wire, or we create a list overlay
    * TODO which records these offsets.  Then, in our non-blocking eventloop
    * TODO which writes data to the MTA child as it goes we simply not write
    * TODO the Bcc: as necessary; how about that? */
   input_save = scp->sc_input;
   if(scp->sc_urlp->url_cproto == CPROTO_NONE &&
         scp->sc_urlp->url_portno != U16_MAX &&
         (resent || (scp->sc_hp != NIL && scp->sc_hp->h_bcc != NIL)) &&
         !ok_blook(mta_bcc_ok)){
      boole inhdr, inskip;
      uz bufsize, bcnt, llen;
      char *buf;
      FILE *fp;

      if((fp = mx_fs_tmp_open(NIL, "mtabccok", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
               NIL)) == NIL){
jewritebcc:
         n_perr(_("Creation of *mta-write-bcc* message"), 0);
         *senderror = TRU1;
         goto jleave;
      }
      scp->sc_input = fp;

      mx_fs_linepool_aquire(&buf, &bufsize);
      bcnt = fsize(input_save);
      inhdr = TRU1;
      inskip = FAL0;
      while(fgetline(&buf, &bufsize, &bcnt, &llen, input_save, TRU1) != NIL){
         if(inhdr){
            if(llen == 1 && *buf == '\n')
               inhdr = FAL0;
            else{
               if(inskip && *buf == ' ')
                  continue;
               inskip = FAL0;
               /* (We need _case for resent only);
                * xxx We do not resent that yet , but place the logic today */
               if(su_cs_starts_with_case(buf, "bcc:")){
                  inskip = TRU1;
                  buf[sizeof("bcc:") -1] = '\n';
                  llen = sizeof("bcc:");
               }else if(resent && su_cs_starts_with_case(buf, "resent-bcc:")){
                  inskip = TRU1;
                  buf[sizeof("resent-bcc:") -1] = '\n';
                  llen = sizeof("resent-bcc:");
               }
            }
         }
         if(fwrite(buf, 1, llen, fp) != llen)
            goto jewritebcc;
      }
      mx_fs_linepool_release(buf, bufsize);

      if(ferror(input_save)){
         *senderror = TRU1;
         goto jleave;
      }
      fflush_rewind(fp);
   }

   rv = TRU1;

   for(cnt = 0, np = scp->sc_to; np != NIL;){
      FILE *ef;

      if((ef = mx_privacy_encrypt_try(scp->sc_input, np->n_name)
            ) != R(FILE*,-1)){
         if(ef != NIL){
            struct mx_name *nsave;
            FILE *fisave;

            fisave = scp->sc_input;
            nsave = scp->sc_to;

            scp->sc_to = ndup(np, np->n_type & ~(GFULL | GFULLEXTRA | GSKIN));
            scp->sc_input = ef;
            if(!a_sendout_mta_start(scp))
               rv = FAL0;
            scp->sc_to = nsave;
            scp->sc_input = fisave;

            mx_fs_close(ef);
         }else{
            n_err(_("Message not sent to: %s\n"), np->n_name);
            _sendout_error = TRU1;
         }

         rewind(scp->sc_input);

         if(np->n_flink != NIL)
            np->n_flink->n_blink = np->n_blink;
         if(np->n_blink != NIL)
            np->n_blink->n_flink = np->n_flink;
         if(np == scp->sc_to)
            scp->sc_to = np->n_flink;
         np = np->n_flink;
      }else{
         ++cnt;
         np = np->n_flink;
      }
   }

   if(cnt > 0 && (mx_privacy_encrypt_is_forced() || !a_sendout_mta_start(scp)))
      rv = FAL0;

jleave:
   if(input_save != scp->sc_input){
      mx_fs_close(scp->sc_input);
      rewind(scp->sc_input = input_save);
   }

   NYD_OU;
   return rv;
} /* }}} */

static boole
a_sendout_mta_start(struct mx_send_ctx *scp){ /* {{{ */
   struct mx_child_ctx cc;
   sigset_t nset;
   char const **args, *mta;
   boole rv, dowait;
   NYD_IN;

   /* Let rv be: TRU1=SMTP, FAL0=file, TRUM1=test */
   mta = scp->sc_urlp->url_input;
   if(scp->sc_urlp->url_cproto == CPROTO_NONE){
      if(scp->sc_urlp->url_portno == 0)
         rv = FAL0;
      else{
         ASSERT(scp->sc_urlp->url_portno == U16_MAX);
         rv = TRUM1;
      }
   }else
      rv = TRU1;

   sigemptyset(&nset);
   sigaddset(&nset, SIGHUP);
   sigaddset(&nset, SIGINT);
   sigaddset(&nset, SIGQUIT);
   sigaddset(&nset, SIGTSTP);
   sigaddset(&nset, SIGTTIN);
   sigaddset(&nset, SIGTTOU);
   mx_child_ctx_setup(&cc);
   dowait = ((n_poption & n_PO_D_V) ||
         (a_sendout_sendwait_to_swf() & a_SENDOUT_SWF_MTA));
   if(rv != TRU1 || dowait)
      cc.cc_flags |= mx_CHILD_SPAWN_CONTROL;
   cc.cc_mask = &nset;

   if(rv != TRU1){
      char const *mta_base;

      if((mta = mx_fexpand(mta_base = mta, mx_FEXP_DEF_LOCAL_FILE_VAR)) == NIL){
         n_err(_("*mta* variable file expansion failure: %s\n"),
            n_shexp_quote_cp(mta_base, FAL0));
         goto jstop;
      }

      if(rv == TRUM1){
         rv = a_sendout_mta_test(scp, mta);
         goto jleave;
      }

      args = a_sendout_mta_file_args(scp->sc_to, scp->sc_hp);
      if(n_poption & n_PO_D){
         a_sendout_mta_file_debug(scp, mta, args);
         rv = TRU1;
         goto jleave;
      }

      /* Wait with control pipe close until after exec */
      ASSERT(cc.cc_flags & mx_CHILD_SPAWN_CONTROL);
      cc.cc_flags |= mx_CHILD_SPAWN_CONTROL_LINGER;
      cc.cc_fds[mx_CHILD_FD_IN] = fileno(scp->sc_input);
   }else/* if(rv == TRU1)*/{
      UNINIT(args, NULL);
#ifndef mx_HAVE_SMTP
      n_err(_("No SMTP support compiled in\n"));
      goto jstop;
#else
      if(n_poption & n_PO_D){
         (void)mx_smtp_mta(scp);
         rv = TRU1;
         goto jleave;
      }

      cc.cc_fds[mx_CHILD_FD_IN] = mx_CHILD_FD_NULL;
      cc.cc_fds[mx_CHILD_FD_OUT] = mx_CHILD_FD_NULL;
#endif
   }

   /* Fork, set up the temporary mail file as standard input for "mail", and
    * exec with the user list we generated far above */
   if(!mx_child_fork(&cc)){
      if(cc.cc_flags & mx_CHILD_SPAWN_CONTROL_LINGER){
         char const *ecp;

         ecp = (cc.cc_error != su_ERR_NOENT) ? su_err_doc(cc.cc_error)
               : _("executable not found (adjust *mta* variable)");
         n_err(_("Cannot start %s: %s\n"), n_shexp_quote_cp(mta, FAL0), ecp);
      }
jstop:
      savedeadletter(scp->sc_input, TRU1);
      n_err(_("... message not sent\n"));
      _sendout_error = TRU1;
   }else if(cc.cc_pid == 0)
      goto jkid;
   else if(dowait){
      /* TODO Now with SPAWN_CONTROL we could actually (1) handle $DEAD only
       * TODO in the parent, and (2) report the REAL child error status!! */
      rv = (mx_child_wait(&cc) && cc.cc_exit_status == su_EX_OK);
      if(!rv)
         goto jstop;
   }else{
      mx_child_forget(&cc);
      rv = TRU1;
   }

jleave:
   NYD_OU;
   return rv;

jkid:
   mx_child_in_child_setup(&cc);

#ifdef mx_HAVE_SMTP
   if(rv == TRU1){
      if(mx_smtp_mta(scp))
         _exit(su_EX_OK);
      savedeadletter(scp->sc_input, TRU1);
      if(!dowait)
         n_err(_("... message not sent\n"));
   }else
#endif
        {
      execv(mta, n_UNCONST(args));
      mx_child_in_child_exec_failed(&cc, su_err());
   }
   for(;;)
      _exit(su_EX_ERR);
} /* }}} */

/* a_sendout_mta_file* {{{ */
static char const **
a_sendout_mta_file_args(struct mx_name *to, struct header *hp)
{
   uz vas_cnt, i, j;
   char **vas;
   char const **args, *cp, *cp_v15compat;
   boole snda;
   NYD_IN;

   if((cp_v15compat = ok_vlook(sendmail_arguments)) != NULL)
      n_OBSOLETE(_("please use *mta-arguments*, not *sendmail-arguments*"));
   if((cp = ok_vlook(mta_arguments)) == NULL)
      cp = cp_v15compat;
   if ((cp /* TODO v15: = ok_vlook(mta_arguments)*/) == NULL) {
      vas_cnt = 0;
      vas = NULL;
   } else {
      /* Don't assume anything on the content but do allocate exactly j slots;
       * like this getrawlist will never overflow (and return -1) */
      j = su_cs_len(cp);
      vas = su_LOFI_ALLOC(sizeof(*vas) * j);
      vas_cnt = S(uz,getrawlist(mx_SCOPE_LOCAL, TRU1, FAL0, vas, j, cp, j));
   }

   i = 4 + n_smopts_cnt + vas_cnt + 4 + 1 + count(to) + 1;
   args = su_AUTO_ALLOC(i * sizeof(char*));

   if((cp_v15compat = ok_vlook(sendmail_progname)) != NULL)
      n_OBSOLETE(_("please use *mta-argv0*, not *sendmail-progname*"));
   cp = ok_vlook(mta_argv0);
   if(cp_v15compat != NULL && !su_cs_cmp(cp, VAL_MTA_ARGV0))
      cp = cp_v15compat;
   args[0] = cp/* TODO v15 only : = ok_vlook(mta_argv0) */;

   if ((snda = ok_blook(sendmail_no_default_arguments)))
      n_OBSOLETE(_("please use *mta-no-default-arguments*, "
         "not *sendmail-no-default-arguments*"));
   snda |= ok_blook(mta_no_default_arguments);
   if ((snda /* TODO v15: = ok_blook(mta_no_default_arguments)*/))
      i = 1;
   else {
      args[1] = "-i";
      i = 2;
      if (ok_blook(metoo))
         args[i++] = "-m";
      if (n_poption & n_PO_V)
         args[i++] = "-v";
   }

   for (j = 0; j < n_smopts_cnt; ++j, ++i)
      args[i] = n_smopts[j];

   for (j = 0; j < vas_cnt; ++j, ++i)
      args[i] = vas[j];

   /* -r option?  In conjunction with -t we act compatible to postfix(1) and
    * ignore it (it is -f / -F there) if the message specified From:/Sender:.
    * The interdependency with -t has been resolved in n_puthead() */
   if (!snda && ((n_poption & n_PO_r_FLAG) || ok_blook(r_option_implicit))) {
      struct mx_name const *np;

      if (hp != NULL && (np = hp->h_from) != NULL) {
         /* However, what wasn't resolved there was the case that the message
          * specified multiple From: addresses and a Sender: */
         if((n_poption & n_PO_t_FLAG) && hp->h_sender != NULL)
            np = hp->h_sender;

         if (np->n_fullextra != NULL) {
            args[i++] = "-F";
            args[i++] = np->n_fullextra;
         }
         cp = np->n_name;
      } else {
         ASSERT(n_poption_arg_r == NULL);
         cp = skin(myorigin(NULL));
      }

      if (cp != NULL) {
         args[i++] = "-f";
         args[i++] = cp;
      }
   }

   /* Terminate option list to avoid false interpretation of system-wide
    * aliases that start with hyphen */
   if (!snda)
      args[i++] = "--";

   /* Recipients follow */
   /* C99 */{
      boole nra_v15compat;

      nra_v15compat = ok_blook(mta_no_receiver_arguments);
      if(nra_v15compat)
         n_OBSOLETE(_("please use mta-no-recipient-arguments not -receiver-; "
            "a \"braino\", sorry!"));

      if(!nra_v15compat && !ok_blook(mta_no_recipient_arguments))
         for(; to != NIL; to = to->n_flink)
            if(!(to->n_type & GDEL))
               args[i++] = to->n_name;
   }
   args[i] = NIL;

   if(vas != NIL)
      su_LOFI_FREE(vas);

   NYD_OU;
   return args;
}

static void
a_sendout_mta_file_debug(struct mx_send_ctx *scp, char const *mta,
      char const **args){
   uz cnt, bufsize, llen;
   char *buf;
   NYD_IN;

   n_err(_(">>> MTA: %s, arguments:"), n_shexp_quote_cp(mta, FAL0));
   for(; *args != NIL; ++args)
      n_err(" %s", n_shexp_quote_cp(*args, FAL0));
   n_err("\n");

   fflush_rewind(scp->sc_input);

   mx_fs_linepool_aquire(&buf, &bufsize);
   cnt = fsize(scp->sc_input);
   while(fgetline(&buf, &bufsize, &cnt, &llen, scp->sc_input, TRU1) != NIL){
      buf[--llen] = '\0';
      n_err(">>> %s\n", buf);
   }
   mx_fs_linepool_release(buf, bufsize);
   NYD_OU;
}
/* }}} */

static boole
a_sendout_mta_test(struct mx_send_ctx *scp, char const *mta){ /* {{{ */
   enum{
      a_OK = 0,
      a_ERR = 1u<<0,
      a_MAFC = 1u<<1,
      a_ANY = 1u<<2,
      a_LASTNL = 1u<<3
   };
   FILE *fp;
   s32 f;
   uz bufsize, cnt, llen;
   char *buf;
   NYD_IN;

   mx_fs_linepool_aquire(&buf, &bufsize);

   if(*mta == '\0')
      fp = n_stdout;
   else{
      if((fp = mx_fs_open(mta, mx_FS_O_RDWR | mx_FS_O_CREATE)) == NIL)
         goto jeno;
      if(!mx_file_lock(fileno(fp), (mx_FILE_LOCK_MODE_TEXCL |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
         f = su_ERR_NOLCK;
         goto jefo;
      }
      if((f = n_folder_mbox_prepare_append(fp, FAL0, NIL)) != su_ERR_NONE)
         goto jefo;
   }

   fflush_rewind(scp->sc_input);
   cnt = fsize(scp->sc_input);
   f = ok_blook(mbox_fcc_and_pcc) ? a_MAFC : a_OK;

   if((f & a_MAFC) &&
         fprintf(fp, "From %s %s", ok_vlook(LOGNAME), mx_time_current.tc_ctime) < 0)
      goto jeno;
   while(fgetline(&buf, &bufsize, &cnt, &llen, scp->sc_input, TRU1) != NIL){
      if(fwrite(buf, 1, llen, fp) != llen)
         goto jeno;
      if(f & a_MAFC){
         f |= a_ANY;
         if(llen > 0 && buf[llen - 1] == '\0')
            f |= a_LASTNL;
         else
            f &= ~a_LASTNL;
      }
   }
   if(ferror(scp->sc_input))
      goto jefo;
   if((f & (a_ANY | a_LASTNL)) == a_ANY && putc('\n', fp) == EOF)
      goto jeno;

jdone:
   if(fp != n_stdout)
      mx_fs_close(fp);
   else
      clearerr(fp);

jleave:
   mx_fs_linepool_release(buf, bufsize);

   NYD_OU;
   return ((f & a_ERR) == 0);

jeno:
   f = su_err();
jefo:
   n_err(_("test MTA: cannot open/prepare/write: %s: %s\n"),
      n_shexp_quote_cp(mta, FAL0), su_err_doc(f));
   f = a_ERR;
   if(fp != NIL)
      goto jdone;
   goto jleave;
} /* }}} */

static char const *
a_sendout_random_id(struct header *hp, boole msgid){ /* {{{ */
   static u32 reprocnt;

   char ibuf[su_IENC_BUFFER_SIZE];
   struct n_string s_b, *s;
   s32 ti;
   struct tm *tmp;
   char const *sender, *host, *fmt, *h;
   uz rl, i;
   char *rv, c, have_r;
   NYD_IN;

   rv = NIL;

   if(msgid && hp != NIL && hp->h_message_id != NIL){
      rv = hp->h_message_id->n_name;
      goto jleave;
   }

   if(ok_blook(message_id_disable))
      goto jleave;

   s = n_string_creat_auto(&s_b);
   s = n_string_reserve(s, 80);
   sender = NIL;

   /* v15compat: manual: IDs are generated with either *hostname* or *from*.
    * v15compat: later: ONLY when *message-id* is set (even empty) */

   host = ok_vlook(hostname);
   c = (host != NIL);
   if(!c || *host == '\0')
      host = n_nodename(TRU1);

   sender = __sendout_ident;
   c |= (sender != NIL);
   if(sender == NIL && hp != NIL){
      sender = skin(myorigin(hp));
      c = (sender != NIL && su_cs_find_c(sender, '@') != NIL); /* v15compat */
   }

   fmt = ok_vlook(message_id);
   if(fmt == NIL || *fmt == '\0'){
      if(!c && fmt == NIL)
         goto jleave;
jfmt:
      fmt = "%Y%m%d%H%M%S.%r%r@%a";
   }

   tmp = &mx_time_current.tc_gm;

   for(have_r = 0; (c = *fmt++) != '\0';){
      if(c != '%' || *fmt == '\0' || (c = *fmt++) == '%'){
jc:
         if(c == '<' || c == '>')
            c = '%';
         s = n_string_push_c(s, c);
      }else switch(c){
      default:
         n_err(_("*message-id*: invalid conversion: %s\n"), &fmt[-2]);
         goto jc;
      case 'a':
         h = sender;
         if(h == NIL)
            goto jh;
         rl = s->s_len;
         s = n_string_push_cp(s, h);
         if((rv = su_cs_find_c(h, '@')) != NIL){
            i = P2UZ(rv - h);
            s->s_dat[rl + i] = '%';
         }
         break;
      case 'd':
         ti = tmp->tm_mday;
         goto jti2;
      case 'H':
         ti = tmp->tm_hour;
         goto jti2;
      case 'h':
jh:      h = host;
         if(h == NIL)
            goto jr;
         s = n_string_push_cp(s, h);
         break;
      case 'M':
         ti = tmp->tm_min;
         goto jti2;
      case 'm':
         ti = tmp->tm_mon + 1;
         goto jti2;
      case 'r':{
jr:      i = s->s_len;
         rl = 4 * (1 + (c != 'r'));
         s = n_string_resize(s, i + rl);
         mx_random_create_buf(&s->s_dat[i], rl, &reprocnt);
         have_r = 1;
         }break;
      case 'S':
         ti = tmp->tm_sec;
jti2:    h = su_ienc_s32(ibuf, ti, 10);
         if(h[1] == '\0')
            *UNCONST(char*,--h) = '0';
         s = n_string_push_buf(s, h, 2);
         break;
      case 'Y':
         ti = tmp->tm_year + 1900;
         h = su_ienc_s32(ibuf, ti, 10);
         ASSERT(h[0] != '\0' && h[1] != '\0' && h[2] != '\0' && h[3] != 0 &&
            h[4] == '\0'); /* In the year 10000 this software is obsolete */
         s = n_string_push_buf(s, h, 4);
         break;
      }
   }

   rv = n_string_cp(s);

   if(!su_state_has(su_STATE_REPRODUCIBLE) &&
         (!have_r || s->s_len < 10)){ /* manual! */
      n_err(_("*message-id*: too short or without randoms: %s\n"), rv);
      s = n_string_trunc(s, 0);
      goto jfmt;
   }

   /* n_string_gut(s); */

jleave:
   NYD_OU;
   return rv;
} /* }}} */

static boole
a_sendout_put_addrline(char const *hname, struct mx_name *np, FILE *fo,
   enum a_sendout_addrline_flags saf)
{
   sz hnlen, col, len;
   enum{
      m_ERROR = 1u<<0,
      m_INIT = 1u<<1,
      m_COMMA = 1u<<2,
      m_NOPF = 1u<<3,
      m_NONAME = 1u<<4,
      m_CSEEN = 1u<<5
   } m;
   NYD_IN;

   m = (saf & GCOMMA) ? m_ERROR | m_COMMA : m_ERROR;

   if((col = hnlen = su_cs_len(hname)) > 0){
#undef _X
#define _X(S)  (col == sizeof(S) -1 && !su_cs_cmp_case(hname, S))
      if (saf & GFILES) {
         ;
      } else if (_X("reply-to:") || _X("mail-followup-to:") ||
            _X("references:") || _X("in-reply-to:") ||
            _X("disposition-notification-to:"))
         m |= m_NOPF | m_NONAME;
      else if (_X("to:") || _X("cc:") || _X("bcc:") || _X("resent-to:"))
         m |= m_NOPF;
#undef _X
   }

   for (; np != NULL; np = np->n_flink) {
      if(np->n_type & GDEL)
         continue;
      if(is_addr_invalid(np,
               ((saf & a_SENDOUT_AL_INC_INVADDR ? 0 : EACM_NOLOG) |
                (m & m_NONAME ? EACM_NONAME : EACM_NONE))) &&
            !(saf & a_SENDOUT_AL_INC_INVADDR))
         continue;

      if ((m & m_NOPF) && is_fileorpipe_addr(np))
         continue;

      if ((m & (m_INIT | m_COMMA)) == (m_INIT | m_COMMA)) {
         if (putc(',', fo) == EOF)
            goto jleave;
         m |= m_CSEEN;
         ++col;
      }

      len = su_cs_len(np->n_fullname);
      if (np->n_type & GREF)
         len += 2;
      ++col; /* The separating space */
      if ((m & m_INIT) && /*col > 1 &&*/
            UCMP(z, col + len, >,
               (np->n_type & GREF ? MIME_LINELEN : 72))) {
         if (fputs("\n ", fo) == EOF)
            goto jleave;
         col = 1;
         m &= ~m_CSEEN;
      } else {
         if(!(m & m_INIT) && fwrite(hname, sizeof *hname, hnlen, fo
               ) != sizeof *hname * hnlen)
            goto jleave;
         if(putc(' ', fo) == EOF)
            goto jleave;
      }
      m = (m & ~m_CSEEN) | m_INIT;

      /* C99 */{
         char *hb;

         /* GREF needs to be placed in angle brackets, but which are missing */
         hb = np->n_fullname;
         if(np->n_type & GREF){
            ASSERT(UCMP(z, len, ==, su_cs_len(np->n_fullname) + 2));
            hb = n_lofi_alloc(len +1);
            len -= 2;
            hb[0] = '<';
            hb[len + 1] = '>';
            hb[len + 2] = '\0';
            su_mem_copy(&hb[1], np->n_fullname, len);
            len += 2;
         }
         len = mx_xmime_write(hb, len, fo,
               ((saf & a_SENDOUT_AL_DOMIME) ? CONV_TOHDR_A : CONV_NONE),
               mx_MIME_DISPLAY_ICONV, NIL, NIL);
         if(np->n_type & GREF)
            n_lofi_free(hb);
      }
      if (len < 0)
         goto jleave;
      col += len;
   }

   if(!(m & m_INIT) || putc('\n', fo) != EOF)
      m ^= m_ERROR;
jleave:
   NYD_OU;
   return ((m & m_ERROR) == 0);
}

static boole
a_sendout_infix_resend(struct header *hp, FILE *fi, FILE *fo,
      struct message *mp, struct mx_name *to, int add_resent)
{
   uz cnt, c, bufsize;
   char *buf;
   char const *cp;
   struct mx_name *fromfield = NULL, *senderfield = NULL, *mdn;
   boole rv;
   NYD_IN;

   rv = FAL0;
   mx_fs_linepool_aquire(&buf, &bufsize);
   cnt = mp->m_size;

   /* Write the Resent-Fields */
   if (add_resent) {
      if(fputs("Resent-", fo) == EOF)
         goto jleave;
      if(mx_sendout_header_date(fo, "Date", FAL0) == -1)
         goto jleave;
      if ((cp = myaddrs(NULL)) != NULL) {
         if (!a_sendout_put_name(cp, GCOMMA, SEND_MBOX, "Resent-From:", fo,
               &fromfield))
            goto jleave;
      }
      /* TODO RFC 5322: Resent-Sender SHOULD NOT be used if it's EQ -From: */
      if ((cp = ok_vlook(sender)) != NULL) {
         if (!a_sendout_put_name(cp, GCOMMA | GNOT_A_LIST, SEND_MBOX,
               "Resent-Sender:", fo, &senderfield))
            goto jleave;
      }
      if (!a_sendout_put_addrline("Resent-To:", to, fo, a_SENDOUT_AL_COMMA))
         goto jleave;
      if (((cp = ok_vlook(stealthmua)) == NULL || !su_cs_cmp(cp, "noagent")) &&
            (cp = a_sendout_random_id(NULL, TRU1)) != NULL &&
            fprintf(fo, "Resent-Message-ID: <%s>\n", cp) < 0)
         goto jleave;
   }

   if((mdn = n_UNCONST(check_from_and_sender(fromfield, senderfield))) == NIL)
      goto jleave;
   if (!_check_dispo_notif(mdn, NULL, fo))
      goto jleave;

   /* Write the original headers */
   while (cnt > 0) {
      if(fgetline(&buf, &bufsize, &cnt, &c, fi, FAL0) == NIL){
         if(ferror(fi))
            goto jleave;
         break;
      }
      if (su_cs_cmp_case_n("status:", buf, 7) &&
            su_cs_cmp_case_n("disposition-notification-to:", buf, 28) &&
            !is_head(buf, c, FAL0)){
         if(fwrite(buf, sizeof *buf, c, fo) != c)
            goto jleave;
      }
      if (cnt > 0 && *buf == '\n')
         break;
   }

   /* Write the message body */
   while(cnt > 0){
      if(fgetline(&buf, &bufsize, &cnt, &c, fi, FAL0) == NIL){
         if(ferror(fi))
            goto jleave;
         break;
      }
      /* Strip trailing empty line (XXX Mailbox object should cover this) */
      if(cnt == 0 && *buf == '\n')
         break;

      if(!(hp->h_flags & HF_MESSAGE_8BITMIME)){ /* TODO hack for resending */
         uz i;

         for(i = 0; i < c; ++i)
            if(S(u8,buf[i]) & 0x80u){
               hp->h_flags |= HF_MESSAGE_8BITMIME;
               break;
            }
      }

      if(fwrite(buf, sizeof *buf, c, fo) != c)
         goto jleave;
   }

   rv = TRU1;
jleave:
   mx_fs_linepool_release(buf, bufsize);

   if(!rv)
      n_err(_("infix_resend: creation of temporary mail file failed\n"));
   NYD_OU;
   return rv;
}

FL boole
mx_sendout_mta_url(struct mx_url *urlp){
   /* TODO In v15 this should simply be url_creat(,ok_vlook(mta).
    * TODO Register a "test" protocol handler, and if the protocol ends up
    * TODO as file and the path is "test", then this is also test.
    * TODO I.e., CPROTO_FILE and CPROTO_TEST.  Until then this is messy */
   char const *mta;
   boole rv;
   NYD_IN;

   rv = FAL0;

   if((mta = ok_vlook(smtp)) == NIL){
      boole issnd;
      u16 pno;
      char const *proto;

      mta = ok_vlook(mta);

      if((proto = ok_vlook(sendmail)) != NIL){ /* v15-compat */
         n_OBSOLETE(_("please use *mta* instead of *sendmail*"));
         /* But use it in favour of default value, then */
         if(!su_cs_cmp(mta, VAL_MTA))
            mta = proto;
      }

      if(su_cs_find_c(mta, ':') == NIL){
         if(su_cs_cmp(mta, "test")){
            pno = 0;
            goto jisfile;
         }
         pno = U16_MAX;
         goto jistest;
      }else if((proto = mx_url_servbyname(mta, &pno, &issnd)) == NIL){
         goto jemta;
      }else if(*proto == '\0'){
         if(pno == 0)
            mta += sizeof("file://") -1;
         else{
            /* test -> stdout, test://X -> X */
            ASSERT(pno == U16_MAX);
jistest:
            mta += sizeof("test") -1;
            if(mta[0] == ':' && mta[1] == '/' && mta[2] == '/')
               mta += 3;
         }
jisfile:
         STRUCT_ZERO(struct mx_url, urlp);
         urlp->url_input = mta;
         urlp->url_portno = pno;
         urlp->url_cproto = CPROTO_NONE;
         rv = TRUM1;
         goto jleave;
      }else if(!issnd)
         goto jemta;
   }else{
      n_OBSOLETE(_("please do not use *smtp*, instead "
         "assign a smtp:// URL to *mta*!"));
      /* For *smtp* the smtp:// protocol was optional; be simple: do not check
       * that *smtp* is misused with file:// or so */
      if(mx_url_servbyname(mta, NIL, NIL) == NIL)
         mta = savecat("smtp://", mta);
   }

#ifdef mx_HAVE_NET
   rv = mx_url_parse(urlp, CPROTO_SMTP, mta);
#endif

   if(!rv)
jemta:
      n_err(_("*mta*: invalid or unsupported value: %s\n"),
         n_shexp_quote_cp(mta, FAL0));
jleave:
   NYD_OU;
   return rv;
}

FL int
n_mail(enum n_mailsend_flags msf, struct mx_name *to, struct mx_name *cc,
   struct mx_name *bcc, char const *subject, struct mx_attachment *attach,
   char const *quotefile)
{
   struct header head;
   struct str in, out;
   boole fullnames;
   NYD_IN;

   su_mem_set(&head, 0, sizeof head);

   /* The given subject may be in RFC1522 format. */
   if (subject != NULL) {
      in.s = n_UNCONST(subject);
      in.l = su_cs_len(subject);
      if(mx_mime_display_from_header(&in, &out,
            /* TODO ???_ISPRINT |*/ mx_MIME_DISPLAY_ICONV))
         head.h_subject = savestrbuf(out.s, out.l);
   }

   fullnames = ok_blook(fullnames);

   head.h_flags = HF_CMD_mail;
   if((head.h_to = to) != NULL){
      if(!fullnames)
         head.h_to = to = a_sendout_fullnames_cleanup(to);
      head.h_mailx_raw_to = n_namelist_dup(to, to->n_type);
   }
   if((head.h_cc = cc) != NULL){
      if(!fullnames)
         head.h_cc = cc = a_sendout_fullnames_cleanup(cc);
      head.h_mailx_raw_cc = n_namelist_dup(cc, cc->n_type);
   }
   if((head.h_bcc = bcc) != NULL){
      if(!fullnames)
         head.h_bcc = bcc = a_sendout_fullnames_cleanup(bcc);
      head.h_mailx_raw_bcc = n_namelist_dup(bcc, bcc->n_type);
   }

   head.h_attach = attach;

   /* TODO n_exit_status only!!?? */n_mail1(msf, mx_SCOPE_NONE, &head, NIL,
      quotefile);

   NYD_OU;
   return 0;
}

FL int
c_sendmail(void *vp){
   int rv;
   NYD_IN;

   rv = a_sendout_sendmail(vp, n_MAILSEND_NONE);

   NYD_OU;
   return rv;
}

FL int
c_Sendmail(void *vp){
   int rv;
   NYD_IN;

   rv = a_sendout_sendmail(vp, n_MAILSEND_RECORD_RECIPIENT);

   NYD_OU;
   return rv;
}

FL enum okay
n_mail1(enum n_mailsend_flags msf, enum mx_scope scope, /* {{{ */
   struct header *hp, struct message *quote, char const *quotefile)
{
#ifdef mx_HAVE_NET
   struct mx_cred_ctx cc;
#endif
   struct mx_url url, *urlp = &url;
   struct n_sigman sm;
   struct mx_send_ctx sctx;
   struct mx_name *to;
   boole dosign, mta_isexe;
   FILE * volatile mtf;
   enum okay volatile rv;
   NYD_IN;

#ifdef mx_HAVE_ICONV
   if(iconvd != R(iconv_t,-1)) /* XXX anyway for mime.c++ :-( */
      n_iconv_close(iconvd);
#endif

   _sendout_error = FAL0;
   __sendout_ident = NULL;
   n_pstate_err_no = su_ERR_INVAL;
   rv = STOP;
   mtf = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL) {
   case 0:
      break;
   default:
      goto jleave;
   }

   /* Update some globals we likely need first */
   mx_time_current_update(NIL, TRU1);

   temporary_compose_mode_hook_control(TRU1, scope);

   /* Collect user's mail from standard input.  Get the result as mtf */
   mtf = n_collect(msf, scope, hp, quote, quotefile, &_sendout_error);
   if (mtf == NULL)
      goto jleave;
   /* TODO All custom headers should be joined here at latest
    * TODO In fact that should happen before we enter compose mode, so that the
    * TODO -C headers can be managed (removed etc.) via ~^, too, but the
    * TODO *customhdr* ones are fixated at this very place here, no sooner! */

   /* */
#ifdef mx_HAVE_PRIVACY
   dosign = TRUM1;
#else
   dosign = FAL0;
#endif

   if(n_psonce & n_PSO_INTERACTIVE){
#ifdef mx_HAVE_PRIVACY
      if(ok_blook(asksign))
         dosign = mx_tty_yesorno(_("Sign this message"), TRU1);
#endif
   }

   if(fsize(mtf) == 0){
      if(n_poption & n_PO_E_FLAG){
         n_pstate_err_no = su_ERR_NONE;
         rv = OKAY;
         goto jleave;
      }

      if(hp->h_subject == NULL)
         n_err(_("No message, no subject; hope that's ok\n"));
      else if(ok_blook(bsdcompat) || ok_blook(bsdmsgs))
         n_err(_("Null message body; hope that's ok\n"));
   }

#ifdef mx_HAVE_PRIVACY
   if(dosign == TRUM1)
      dosign = mx_privacy_sign_is_desired(); /* TODO USER@HOST, *from*++!!! */
#endif

   /* XXX Update time_current again; once n_collect() offers editing of more
    * XXX headers, including Date:, this must only happen if Date: is the
    * XXX same that it was before n_collect() (e.g., postponing etc.).
    * XXX But *do* update otherwise because the mail seems to be backdated
    * XXX if the user edited some time, which looks odd and it happened
    * XXX to me that i got mis-dated response mails due to that... */
   mx_time_current_update(NIL, TRU1);

   /* TODO hrmpf; the MIME/send layer rewrite MUST address the init crap:
    * TODO setup the header ONCE; note this affects edit.c, collect.c ...,
    * TODO but: offer a hook that rebuilds/expands/checks/fixates all
    * TODO header fields ONCE, call that ONCE after user editing etc. has
    * TODO completed (one edit cycle) */

   if(!(mta_isexe = mx_sendout_mta_url(urlp)))
      goto jfail_dead;
   mta_isexe = (mta_isexe != TRU1);

   to = n_namelist_vaporise_head(hp, (EACM_NORMAL | EACM_DOMAINCHECK |
            (mta_isexe ? EACM_NONE : EACM_NONAME | EACM_NONAME_OR_FAIL)),
         &_sendout_error);

   if(_sendout_error < 0){
      n_err(_("Some addressees were classified as \"hard error\"\n"));
      n_pstate_err_no = su_ERR_PERM;
      goto jfail_dead;
   }
   if(to == NULL){
      n_err(_("No recipients specified\n"));
      n_pstate_err_no = su_ERR_DESTADDRREQ;
      goto jfail_dead;
   }

   /* */
   STRUCT_ZERO(struct mx_send_ctx, &sctx);
   sctx.sc_hp = hp;
   sctx.sc_to = to;
   sctx.sc_input = mtf;
   sctx.sc_urlp = urlp;
#ifdef mx_HAVE_NET
   sctx.sc_credp = &cc;
#endif

   if((dosign || count_nonlocal(to) > 0) &&
         !a_sendout_setup_creds(&sctx, (dosign > 0))){
      /* TODO saving $DEAD and recovering etc is not yet well defined */
      n_pstate_err_no = su_ERR_INVAL;
      goto jfail_dead;
   }

   /* C99 */{
      boole x;

      x = a_sendout_infix(&sctx, dosign);
      mtf = sctx.sc_input;
      if(!x)
         goto jfail_dead;
   }

   /*  */
#ifdef mx_HAVE_PRIVACY
   if(dosign){
      FILE *nmtf;

      if((nmtf = mx_privacy_sign(mtf, sctx.sc_signer.s)) == NIL)
         goto jfail_dead;

      mx_fs_close(mtf);
      mtf = nmtf;
   }
#endif

   /* TODO truly - i still don't get what follows: (1) we deliver file
    * TODO and pipe addressees, (2) we mightrecord() and (3) we transfer
    * TODO even if (1) savedeadletter() etc.  To me this doesn't make sense? */

   /* C99 */{
      u32 cnt;
      boole b;

      /* Deliver pipe and file addressees */
      b = (ok_blook(record_files) && count(to) > 0);
      to = a_sendout_file_a_pipe(to, mtf, &_sendout_error);

      if (_sendout_error)
         savedeadletter(mtf, FAL0);

      to = elide(to); /* XXX only to drop GDELs due a_sendout_file_a_pipe()! */
      cnt = count(to);

      if(((msf & n_MAILSEND_RECORD_RECIPIENT) || b || cnt > 0) &&
            !a_sendout_mightrecord(mtf,
               (msf & n_MAILSEND_RECORD_RECIPIENT ? to : NIL), FAL0))
         goto jleave;
      if (cnt > 0) {
         sctx.sc_hp = hp;
         sctx.sc_to = to;
         sctx.sc_input = mtf;
         b = FAL0;
         if(a_sendout_transfer(&sctx, FAL0, &b))
            rv = OKAY;
         else if(b && _sendout_error == 0){
            _sendout_error = b;
            savedeadletter(mtf, FAL0);
         }
      } else if (!_sendout_error)
         rv = OKAY;
   }

   n_sigman_cleanup_ping(&sm);
jleave:
   if(mtf != NIL){
      char const *cp;

      mx_fs_close(mtf);

      if((cp = ok_vlook(on_compose_cleanup)) != NIL)
         temporary_compose_mode_hook_call(cp, FAL0);
   }

   temporary_compose_mode_hook_control(FAL0, mx_SCOPE_NONE);

   if(_sendout_error){
      n_psonce |= n_PSO_SEND_ERROR;
      n_exit_status |= n_EXIT_SEND_ERROR;
   }
   if(rv == OKAY)
      n_pstate_err_no = su_ERR_NONE;

   NYD_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;

jfail_dead:
   _sendout_error = TRU1;
   savedeadletter(mtf, TRU1);
   n_err(_("... message not sent\n"));
   goto jleave;
} /* }}} */

FL boole
n_puthead(enum sendaction action, boole nosend_msg, FILE *fo, /*{{{*/
      struct header *hp, enum gfield w){
#define a_SENDOUT_PUT_CC_BCC_FCC() \
do{\
   if((w & GCC) && (hp->h_cc != NIL || nosend_msg == TRUM1)){\
      if(!a_sendout_put_addrline("Cc:", hp->h_cc, fo, saf))\
         goto jleave;\
      ++gotcha;\
   }\
   if((w & GBCC) && (hp->h_bcc != NIL || nosend_msg == TRUM1)){\
      if(!a_sendout_put_addrline("Bcc:", hp->h_bcc, fo, saf))\
         goto jleave;\
      ++gotcha;\
   }\
   if((w & GBCC_IS_FCC) && nosend_msg){\
      for(np = hp->h_fcc; np != NIL; np = np->n_flink){\
         if(fprintf(fo, "Fcc: %s\n", np->n_name) < 0)\
            goto jleave;\
         ++gotcha;\
      }\
   }\
}while(0)

   char const *addr;
   uz gotcha;
   int stealthmua;
   boole nodisp;
   enum a_sendout_addrline_flags saf;
   struct mx_name *np, *fromasender, *mft, **mftp;
   boole rv;
   NYD_IN;
#ifdef mx_HAVE_ICONV
   ASSERT(!nosend_msg || iconvd == R(iconv_t,-1));
#endif

   mftp = NIL;
   fromasender = mft = NIL;
   rv = FAL0;

   if(nosend_msg && (hp->h_flags & HF_COMPOSE_MODE) &&
         !(hp->h_flags & HF_USER_EDITED)){
      if(fputs(_("# Message will be discarded unless file is saved\n"),
            fo) == EOF)
         goto jleave;
   }

   if ((addr = ok_vlook(stealthmua)) != NULL)
      stealthmua = !su_cs_cmp(addr, "noagent") ? -1 : 1;
   else
      stealthmua = 0;
   gotcha = 0;
   nodisp = (action != SEND_TODISP);
   saf = (w & (GCOMMA | GFILES)) | (nodisp ? a_SENDOUT_AL_DOMIME : 0);
   if(nosend_msg)
      saf |= a_SENDOUT_AL_INC_INVADDR;

   if(w & GDATE)
      mx_sendout_header_date(fo, "Date", FAL0), ++gotcha;

   if (w & GIDENT) {
      if (hp->h_from == NULL || hp->h_sender == NULL)
         setup_from_and_sender(hp);

      if(hp->h_from != NIL){
         if(hp->h_author != NIL){
            if(!a_sendout_put_addrline("Author:", hp->h_author, fo, saf))
               goto jleave;
         }else if(!a_sendout_put_addrline("Author:", hp->h_from, fo, saf))
            goto jleave;
         if(!a_sendout_put_addrline("From:", hp->h_from, fo, saf))
            goto jleave;
         ++gotcha;
      }

      if (hp->h_sender != NULL) {
         if (!a_sendout_put_addrline("Sender:", hp->h_sender, fo, saf))
            goto jleave;
         ++gotcha;
      }

      fromasender = n_UNCONST(check_from_and_sender(hp->h_from, hp->h_sender));
      if (fromasender == NULL)
         goto jleave;
      /* Note that fromasender is (NULL,) 0x1 or real sender here */
   }

   /* M-F-T: check this now, and possibly place us in Cc: */
   if((w & GIDENT) && !nosend_msg){
      /* Mail-Followup-To: TODO factor out this huge block of code.
       * TODO Also, this performs multiple expensive list operations, which
       * TODO hopefully can be heavily optimized later on! */
      /* Place ourselves in there if any non-subscribed list is an addressee */
      if((hp->h_flags & HF_LIST_REPLY) || hp->h_mft != NIL ||
            ok_blook(followup_to)){
         enum{
            a_HADMFT = 1u<<(HF__NEXT_SHIFT + 0),
            a_WASINMFT = 1u<<(HF__NEXT_SHIFT + 1),
            a_ANYLIST = 1u<<(HF__NEXT_SHIFT + 2),
            a_OTHER = 1u<<(HF__NEXT_SHIFT + 3)
         };
         struct mx_name *x;
         u32 f;

         f = hp->h_flags | (hp->h_mft != NIL ? a_HADMFT : 0);
         if(f & a_HADMFT){
            /* Detect whether we were part of the former MFT:.
             * Throw away MFT: if we were the sole member (kidding) */
            hp->h_mft = mft = elide(hp->h_mft);
            mft = mx_alternates_remove(n_namelist_dup(mft, GNONE), FAL0);
            if(mft == NIL)
               f ^= a_HADMFT;
            else for(x = hp->h_mft; x != NIL;
                  x = x->n_flink, mft = mft->n_flink){
               if(mft == NIL){
                  f |= a_WASINMFT;
                  break;
               }
            }
         }

         /* But for that, remove all incarnations of ourselves first.
          * TODO It is total crap that we have alternates_remove(), is_myname()
          * TODO or whatever; these work only with variables, not with data
          * TODO that is _currently_ in some header fields!!!  v15.0: complete
          * TODO rewrite, object based, lazy evaluated, on-the-fly marked.
          * TODO then this should be a really cheap thing in here... */
         np = elide(mx_alternates_remove(cat(
               n_namelist_dup(hp->h_to, GEXTRA | GFULL),
               n_namelist_dup(hp->h_cc, GEXTRA | GFULL)), FAL0));
         addr = hp->h_list_post;
         mft = NIL;
         mftp = &mft;

         while((x = np) != NIL){
            s8 ml;

            np = np->n_flink;

            /* Automatically make MLIST_KNOWN List-Post: address */
            /* XXX mx_mlist_query_mp()?? */
            if(((ml = mx_mlist_query(x->n_name, FAL0)) == mx_MLIST_OTHER ||
                     ml == mx_MLIST_POSSIBLY) &&
                  addr != NIL && !su_cs_cmp_case(addr, x->n_name))
               ml = mx_MLIST_KNOWN;

            /* Any non-subscribed list?  Add ourselves */
            switch(ml){
            case mx_MLIST_KNOWN:
               f |= HF_MFT_SENDER;
               /* FALLTHRU */
            case mx_MLIST_SUBSCRIBED:
               f |= a_ANYLIST;
               goto j_mft_add;
            case mx_MLIST_OTHER:
            case mx_MLIST_POSSIBLY:
               f |= a_OTHER;
               if(!(f & HF_LIST_REPLY)){
j_mft_add:
                  if(!is_addr_invalid(x,
                        EACM_STRICT | EACM_NOLOG | EACM_NONAME)){
                     x->n_blink = *mftp;
                     x->n_flink = NIL;
                     *mftp = x;
                     mftp = &x->n_flink;
                  } /* XXX write some warning?  if verbose?? */
                  continue;
               }
               /* And if this is a reply that honoured a M-F-T: header then
                * we'll also add all members of the original M-F-T: that are
                * still addressed by us, regardless of other circumstances */
               /* TODO If the user edited this list, then we should include
                * TODO whatever she did therewith, even if _LIST_REPLY! */
               else if(f & a_HADMFT){
                  struct mx_name *ox;

                  for(ox = hp->h_mft; ox != NIL; ox = ox->n_flink)
                     if(!su_cs_cmp_case(ox->n_name, x->n_name))
                        goto j_mft_add;
               }
               break;
            }
         }

         if((f & (a_ANYLIST | a_HADMFT)) && mft != NIL){
            if(((f & HF_MFT_SENDER) ||
                     ((f & (a_ANYLIST | a_HADMFT)) == a_HADMFT)) &&
                  (np = fromasender) != NIL && np != R(struct mx_name*,0x1)){
               *mftp = ndup(np, (np->n_type & ~GMASK) | GEXTRA | GFULL);

               /* Place ourselves in the Cc: if we will be a member of M-F-T:,
                * and we are not subscribed (and are no addressee yet)? */
               /* TODO This entire block is much to expensive and should
                * TODO be somewhere else (like name_unite(), or so) */
               if(ok_blook(followup_to_add_cc)){
                  struct mx_name **npp;

                  np = ndup(np, (np->n_type & ~GMASK) | GCC | GFULL);
                  np = cat(cat(hp->h_to, hp->h_cc), np);
                  np = mx_alternates_remove(np, TRU1);
                  np = elide(np);
                  hp->h_to = hp->h_cc = NIL;
                  for(; np != NIL; np = np->n_flink){
                     switch(np->n_type & (GDEL | GMASK)){
                     case GTO: npp = &hp->h_to; break;
                     case GCC: npp = &hp->h_cc; break;
                     default: continue;
                     }
                     *npp = cat(*npp, ndup(np, np->n_type | GFULL));
                  }
               }
            }
         }else
            mft = NIL;
      }
   }

   if(nosend_msg && (hp->h_flags & HF_COMPOSE_MODE) &&
         !(hp->h_flags & HF_USER_EDITED)){
      if(fputs(_("# To:, Cc: and Bcc: support a ?single modifier: "
            "To?: exa, <m@ple>\n"), fo) == EOF)
         goto jleave;
      ++gotcha;
   }

   if((w & GTO) && (hp->h_to != NULL || (nosend_msg &&
         (hp->h_flags & HF_COMPOSE_MODE)))) {
      if(!a_sendout_put_addrline("To:", hp->h_to, fo, saf))
         goto jleave;
      ++gotcha;
   }

   if(!ok_blook(bsdcompat) && !ok_blook(bsdorder))
      a_SENDOUT_PUT_CC_BCC_FCC();

   if((w & GSUBJECT) && ((hp->h_subject != NIL && *hp->h_subject != '\0') ||
         (nosend_msg && (hp->h_flags & HF_COMPOSE_MODE)))){
      if(fwrite("Subject: ", sizeof(char), 9, fo) != 9 ||
            (hp->h_subject != NIL &&
             mx_xmime_write(hp->h_subject, su_cs_len(hp->h_subject), fo,
               (!nodisp ? CONV_NONE : CONV_TOHDR),
               (!nodisp ? mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT
                  : mx_MIME_DISPLAY_ICONV), NIL,NIL) < 0))
         goto jleave;
      putc('\n', fo);
      ++gotcha;
   }

   if (ok_blook(bsdcompat) || ok_blook(bsdorder))
      a_SENDOUT_PUT_CC_BCC_FCC();

   if ((w & GMSGID) && stealthmua <= 0 &&
         (addr = a_sendout_random_id(hp, TRU1)) != NULL) {
      if (fprintf(fo, "Message-ID: <%s>\n", addr) < 0)
         goto jleave;
      ++gotcha;
   }

   if(w & (GREF | GREF_IRT)){
      if((np = hp->h_in_reply_to) == NULL)
         hp->h_in_reply_to = np = n_header_setup_in_reply_to(hp);
      if(np != NULL){
         if(nosend_msg && (hp->h_flags & HF_COMPOSE_MODE) &&
               !(hp->h_flags & HF_USER_EDITED)){
            if(fputs(_("# Removing or modifying In-Reply-To: "
                     "breaks the old, and starts a new thread.\n"
                  "# Assigning hyphen-minus - creates a thread of only the "
                     "replied-to message\n"), fo) == EOF)
               goto jleave;
         }
         if(!a_sendout_put_addrline("In-Reply-To:", np, fo, 0))
            goto jleave;
         ++gotcha;
      }

      if((w & GREF) && (np = hp->h_ref) != NULL){
         if(!a_sendout_put_addrline("References:", np, fo, 0))
            goto jleave;
         ++gotcha;
      }
   }

   if(w & GIDENT) do /*for break*/{
      /* Reply-To:.  Be careful not to destroy a possible user input, duplicate
       * the list first.. TODO it is a terrible codebase.. */
      if((np = hp->h_reply_to) != NIL){
         np = n_namelist_dup(np, np->n_type);
         if((np = usermap(np, TRU1)) == NIL)
            break;
         if((np = checkaddrs(np,
                  (EACM_STRICT | EACM_NONAME | EACM_NOLOG), NIL)) == NIL)
            break;
      }else if((addr = ok_vlook(reply_to)) != NIL)
         np = lextract(addr, GEXTRA |
               (ok_blook(fullnames) ? GFULL | GSKIN : GSKIN));
      else
         break;
      if((np = elide(np)) == NIL)
         break;
      if(!a_sendout_put_addrline("Reply-To:", np, fo, saf))
         goto jleave;
      ++gotcha;
   }while(0);

   if((w & GIDENT) && !nosend_msg){
      if(mft != NIL){
         if(!a_sendout_put_addrline("Mail-Followup-To:", mft, fo, saf))
            goto jleave;
         ++gotcha;
      }

      if(!_check_dispo_notif(fromasender, hp, fo))
         goto jleave;
   }

   if ((w & GUA) && stealthmua == 0) {
      if (fprintf(fo, "User-Agent: %s %s\n", n_uagent,
            (su_state_has(su_STATE_REPRODUCIBLE)
               ? su_reproducible_build : ok_vlook(version))) < 0)
         goto jleave;
      ++gotcha;
   }

   /* Custom headers, as via -C and *customhdr* TODO JOINED AFTER COMPOSE! */
   if(!nosend_msg){
      struct n_header_field *chlp[2], *hfp;
      u32 i;

      chlp[0] = n_poption_arg_C;
      chlp[1] = n_customhdr_list;

      for(i = 0; i < NELEM(chlp); ++i)
         if((hfp = chlp[i]) != NULL){
            if(!_sendout_header_list(fo, hfp, nodisp))
               goto jleave;
            ++gotcha;
         }
   }

   /* The user may have placed headers when editing */
   if(1){
      struct n_header_field *hfp;

      if((hfp = hp->h_user_headers) != NULL){
         if(!_sendout_header_list(fo, hfp, nodisp))
            goto jleave;
         ++gotcha;
      }
   }

   if(gotcha && (w & GNL) && putc('\n', fo) == EOF)
      goto jleave;

   rv = TRU1;
jleave:
   NYD_OU;
   return rv;
#undef a_SENDOUT_PUT_CC_BCC_FCC
} /* }}} */

FL int
mx_sendout_header_date(FILE *fo, char const *field, boole must_locale){
   int tzdiff_hour, tzdiff_min, rv;
   char const *tzsign;
   struct tm *tmptr;
   NYD2_IN;

   tmptr = &mx_time_current.tc_local;

   /* */
   tzsign = n_hy;
   if(LIKELY(tmptr->tm_sec == mx_time_current.tc_gm.tm_sec) || must_locale){
      tzdiff_min = S(int,mx_time_tzdiff(mx_time_current.tc_time, NIL, tmptr));
      tzdiff_min /= su_TIME_MIN_SECS;
      tzdiff_hour = tzdiff_min / su_TIME_HOUR_MINS;
      tzdiff_min %= su_TIME_HOUR_MINS;
      tzdiff_hour *= 100;
      tzdiff_hour += tzdiff_min;
      if(tzdiff_hour < 0)
         tzdiff_hour = -tzdiff_hour;
      else
         tzsign = "+";
   }else{
      static boole a_noted;

      if(!a_noted){
         a_noted = TRU1;
         n_err(_("The difference of UTC to local timezone $TZ requires second precision.\n"
            "  Unsupported by RFC 5321, henceforth using TZ=UTC to not loose precision!\n"));
      }
      tmptr = &mx_time_current.tc_gm;
      tzdiff_hour = 0;
   }

   rv = fprintf(fo, "%s: %s, %02d %s %04d %02d:%02d:%02d %s%04d\n",
         field,
         su_time_weekday_names_abbrev[tmptr->tm_wday],
         tmptr->tm_mday,
         su_time_month_names_abbrev[tmptr->tm_mon],
         tmptr->tm_year + 1900,
         tmptr->tm_hour, tmptr->tm_min, tmptr->tm_sec,
         tzsign, tzdiff_hour);
   if(rv < 0)
      rv = -1;

   NYD2_OU;
   return rv;
}

FL enum okay
n_resend_msg(enum mx_scope scope, struct message *mp, struct mx_url *urlp,
   struct header *hp, boole add_resent)
{
#ifdef mx_HAVE_NET
   struct mx_cred_ctx cc;
#endif
   struct n_sigman sm;
   struct mx_send_ctx sctx;
   FILE * volatile ibuf, *nfo, * volatile nfi;
   struct mx_fs_tmp_ctx *fstcp;
   struct mx_name *to;
   enum okay volatile rv;
   NYD_IN;

   _sendout_error = FAL0;
   __sendout_ident = NULL;
   n_pstate_err_no = su_ERR_INVAL;

   rv = STOP;
   to = hp->h_to;
   ASSERT(hp->h_cc == NIL);
   ASSERT(hp->h_bcc == NIL);
   nfi = ibuf = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL) {
   case 0:
      break;
   default:
      goto jleave;
   }

   /* Update some globals we likely need first */
   mx_time_current_update(NIL, TRU1);

   if((nfo = mx_fs_tmp_open(NIL, "resend", (mx_FS_O_WRONLY | mx_FS_O_HOLDSIGS),
            &fstcp)) == NIL){
      _sendout_error = TRU1;
      n_perr(_("resend_msg: temporary mail file"), 0);
      n_pstate_err_no = su_ERR_IO;
      goto jleave;
   }

   if((nfi = mx_fs_open(fstcp->fstc_filename, mx_FS_O_RDONLY)) == NIL){
      n_perr(fstcp->fstc_filename, 0);
      n_pstate_err_no = su_ERR_IO;
   }

   mx_fs_tmp_release(fstcp);

   if(nfi == NIL)
      goto jerr_o;

   if((ibuf = setinput(&mb, mp, NEED_BODY)) == NULL){
      n_pstate_err_no = su_ERR_IO;
      goto jerr_io;
   }

   temporary_compose_mode_hook_control(TRU1, scope);

   /* C99 */{
      char const *cp;

      if((cp = ok_vlook(on_resend_enter)) != NIL){
         /*setup_from_and_sender(hp);*/
         temporary_compose_mode_hook_call(cp, FAL0);
      }
   }

   su_mem_set(&sctx, 0, sizeof sctx);
   sctx.sc_hp = hp;
   sctx.sc_to = to;
   sctx.sc_input = nfi;
   sctx.sc_urlp = urlp;
#ifdef mx_HAVE_NET
   sctx.sc_credp = &cc;
#endif

   /* All the complicated address massage things happened in the callee(s) */
   if(!_sendout_error &&
         count_nonlocal(to) > 0 && !a_sendout_setup_creds(&sctx, FAL0)){
      /* ..wait until we can write DEAD */
      n_pstate_err_no = su_ERR_INVAL;
      _sendout_error = -1;
   }

   if(!a_sendout_infix_resend(hp, ibuf, nfo, mp, to, add_resent)){
jfail_dead:
      savedeadletter(nfi, TRU1);
      n_err(_("... message not sent\n"));
jerr_io:
      mx_fs_close(nfi);
      nfi = NIL;
jerr_o:
      mx_fs_close(nfo);
      _sendout_error = TRU1;
      goto jleave;
   }

   if(_sendout_error < 0)
      goto jfail_dead;

   mx_fs_close(nfo);
   rewind(nfi);

   /* C99 */{
      boole b, c;

      /* Deliver pipe and file addressees */
      b = (ok_blook(record_files) && count(to) > 0);
      to = a_sendout_file_a_pipe(to, nfi, &_sendout_error);

      if(_sendout_error)
         savedeadletter(nfi, FAL0);

      to = elide(to); /* XXX only to drop GDELs due a_sendout_file_a_pipe()! */
      c = (count(to) > 0);

      if(b || c){
         if(!ok_blook(record_resent) || a_sendout_mightrecord(nfi, NIL, TRU1)){
            sctx.sc_to = to;
            /*sctx.sc_input = nfi;*/
            if(!c || a_sendout_transfer(&sctx, TRU1, &b))
               rv = OKAY;
            else if(b && _sendout_error == 0){
               _sendout_error = b;
               savedeadletter(nfi, FAL0);
            }
         }
      }else if(!_sendout_error)
         rv = OKAY;
   }

   n_sigman_cleanup_ping(&sm);
jleave:
   if(nfi != NIL){
      char const *cp;

      mx_fs_close(nfi);

      if(ibuf != NIL){
         if((cp = ok_vlook(on_resend_cleanup)) != NIL)
            temporary_compose_mode_hook_call(cp, FAL0);

         temporary_compose_mode_hook_control(FAL0, mx_SCOPE_NONE);
      }
   }

   if(_sendout_error){
      n_psonce |= n_PSO_SEND_ERROR;
      n_exit_status |= n_EXIT_SEND_ERROR;
   }
   if(rv == OKAY)
      n_pstate_err_no = su_ERR_NONE;

   NYD_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

FL void
savedeadletter(FILE *fp, boole fflush_rewind_first){ /* {{{ */
   struct n_string line;
   int c;
   enum {a_NONE, a_INIT = 1<<0, a_BODY = 1<<1, a_NL = 1<<2} flags;
   ul bytes, lines;
   FILE *dbuf;
   char const *cp, *cpq;
   NYD_IN;

   if(!ok_blook(save))
      goto jleave;

   if(fflush_rewind_first){
      fflush(fp);
      rewind(fp);
   }
   if(fsize(fp) == 0)
      goto jleave;

   cp = n_getdeadletter();
   cpq = n_shexp_quote_cp(cp, FAL0);

   if(n_poption & n_PO_D){
      n_err(_(">>> Would (try to) write $DEAD %s\n"), cpq);
      goto jleave;
   }

   if((dbuf = mx_fs_open(cp, (mx_FS_O_WRONLY | mx_FS_O_CREATE | mx_FS_O_TRUNC))
            ) == NIL || !mx_file_lock(fileno(dbuf), (mx_FILE_LOCK_MODE_TEXCL |
         mx_FILE_LOCK_MODE_RETRY))){
      n_perr(_("Cannot save to $DEAD"), 0);

      if(dbuf != NIL)
         mx_fs_close(dbuf);
      goto jleave;
   }

   fprintf(n_stdout, "%s ", cpq);
   fflush(n_stdout);

   /* TODO savedeadletter() non-conforming: should check whether we have any
    * TODO headers, if not we need to place "something", anything will do.
    * TODO MIME is completely missing, we use MBOXO quoting!!  Yuck.
    * TODO I/O error handling missing.  Yuck! */
   n_string_reserve(n_string_creat_auto(&line), 2 * SEND_LINESIZE);
   bytes = (ul)fprintf(dbuf, "From %s %s",
         ok_vlook(LOGNAME), mx_time_current.tc_ctime);
   lines = 1;
   for(flags = a_NONE, c = '\0'; c != EOF; bytes += line.s_len, ++lines){
      n_string_trunc(&line, 0);
      while((c = getc(fp)) != EOF && c != '\n')
         n_string_push_c(&line, c);

      /* TODO It may be that we have only some plain text.  It may be that we
       * TODO have a complete MIME encoded message.  We don't know, and we
       * TODO have no usable mechanism to dig it!!  We need v15! */
      if(!(flags & a_INIT)){
         uz i;

         /* Throw away leading empty lines! */
         if(line.s_len == 0)
            continue;
         for(i = 0; i < line.s_len; ++i){
            if(fieldnamechar(line.s_dat[i]))
               continue;
            if(line.s_dat[i] == ':'){
               flags |= a_INIT;
               break;
            }else{
               /* We have no headers, this is already a body line! */
               flags |= a_INIT | a_BODY;
               break;
            }
         }
         /* Well, i had to check whether the RFC allows this.  Assume we've
          * passed the headers, too, then! */
         if(i == line.s_len)
            flags |= a_INIT | a_BODY;
      }
      if(flags & a_BODY){
         if(line.s_len >= 5 && !su_mem_cmp(line.s_dat, "From ", 5))
            n_string_unshift_c(&line, '>');
      }
      if(line.s_len == 0)
         flags |= a_BODY | a_NL;
      else
         flags &= ~a_NL;

      n_string_push_c(&line, '\n');
      fwrite(line.s_dat, sizeof *line.s_dat, line.s_len, dbuf);
   }
   if(!(flags & a_NL)){
      putc('\n', dbuf);
      ++bytes;
      ++lines;
   }
   n_string_gut(&line);

   mx_fs_close(dbuf);
   fprintf(n_stdout, "%lu/%lu\n", lines, bytes);
   fflush(n_stdout);

   rewind(fp);
jleave:
   NYD_OU;
} /* }}} */

#ifdef mx_HAVE_REGEX
FL boole
mx_sendout_temporary_digdump(FILE *ofp, struct mimepart *mp, /* {{{ */
      struct header *envelope_or_nil, boole is_main_mp){
   /* It is a terrible hack; we need a DOM and just dump_to_wire() */
   FILE *ifp;
   uz linesize, cnt, len, i;
   char *linedat, *cp;
   boole rv;
   NYD2_IN;

   rv = FAL0;
   mx_fs_linepool_aquire(&linedat, &linesize);

   if((ifp = setinput(&mb, R(struct message*,mp), NEED_BODY)) == NIL)
      goto jleave;

   cnt = mp->m_size;
   while(fgetline(&linedat, &linesize, &cnt, &len, ifp, TRU1) != NIL){
      if(!is_main_mp){
         for(cp = linedat;;){
            if((i = fwrite(cp, 1, len, ofp)) == 0)
               break;
            len -= i;
            if(len == 0)
               break;
            cp += i;
         }
         if(len != 0)
            goto jleave;
      }else if(len == 1)
         is_main_mp = FAL0;

      if(envelope_or_nil != NIL) /* xxx From_ line aka postfix */
         break;
   }

   if(envelope_or_nil != NIL){
# define a_SENDOUT_PUT_CC_BCC_FCC() \
do{\
   if(hp->h_cc != NIL && !a_sendout_put_addrline("Cc:", hp->h_cc, ofp, saf))\
      goto jleave;\
   if(hp->h_bcc != NIL && !a_sendout_put_addrline("Bcc:", hp->h_bcc, ofp,saf))\
      goto jleave;\
}while(0)

      BITENUM(u32,a_sendout_addrline_flags) const saf =
            a_SENDOUT_AL_DOMIME | a_SENDOUT_AL_COMMA;

      struct n_header_field *chlp[3], *hfp;
      struct mx_name *np;
      struct header *hp;

      hp = envelope_or_nil;
      envelope_or_nil = NIL;

      if(hp->h_from == NIL || hp->h_sender == NIL)
         setup_from_and_sender(hp);

      if((np = hp->h_author) != NIL &&
            !a_sendout_put_addrline("Author:", np, ofp, saf))
         goto jleave;
      if((np = hp->h_from) != NIL){
         if(hp->h_author == NIL &&
               !a_sendout_put_addrline("Author:", np, ofp, saf))
            goto jleave;
         if(!a_sendout_put_addrline("From:", np, ofp, saf))
            goto jleave;
      }
      if((np = hp->h_sender) != NIL &&
            !a_sendout_put_addrline("Sender:", np, ofp, saf))
         goto jleave;

      if((np = hp->h_to) != NIL &&
            !a_sendout_put_addrline("To:", np, ofp, saf))
         goto jleave;
      if(!ok_blook(bsdcompat) && !ok_blook(bsdorder))
         a_SENDOUT_PUT_CC_BCC_FCC();

      if(fwrite("Subject: ", sizeof(char), 9, ofp) != 9 ||
            (hp->h_subject != NIL &&
             mx_xmime_write(hp->h_subject,
               su_cs_len(hp->h_subject), ofp,
               CONV_TOHDR, mx_MIME_DISPLAY_ICONV, NIL,NIL) < 0))
         goto jleave;
      putc('\n', ofp);

      if(ok_blook(bsdcompat) || ok_blook(bsdorder))
         a_SENDOUT_PUT_CC_BCC_FCC();

      if((np = hp->h_in_reply_to) != NIL &&
            !a_sendout_put_addrline("In-Reply-To:", np, ofp, saf))
         goto jleave;
      if((np = hp->h_ref) != NIL &&
            !a_sendout_put_addrline("References:", np, ofp, saf))
         goto jleave;
      if((np = hp->h_reply_to) != NIL &&
            !a_sendout_put_addrline("Reply-To:", np, ofp, saf))
         goto jleave;

      chlp[0] = n_poption_arg_C;
      chlp[1] = n_customhdr_list;
      chlp[2] = hp->h_user_headers;
      for(i = 0; i < NELEM(chlp); ++i){
         if((hfp = chlp[i]) != NIL && !_sendout_header_list(ofp, hfp, TRU1))
            goto jleave;
      }

      if(putc('\n', ofp) == EOF)
         goto jleave;
# undef a_SENDOUT_PUT_CC_BCC_FCC
   }

   rv = TRU1;
jleave:
   mx_fs_linepool_release(linedat, linesize);

   NYD2_OU;
   return rv;
} /* }}} */
#endif /* mx_HAVE_REGEX */

#undef SEND_LINESIZE

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_SENDOUT
/* s-it-mode */
