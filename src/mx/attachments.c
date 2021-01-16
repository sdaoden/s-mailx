/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of attachments.h.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE attachments
#define mx_SOURCE
#define mx_SOURCE_ATTACHMENTS

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/file-streams.h"
#include "mx/go.h"
#include "mx/iconv.h"
#include "mx/mime-type.h"
#include "mx/sigs.h"
#include "mx/tty.h"

#include "mx/attachments.h"
/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* We use calloc() for struct mx_attachment */
CTAV(mx_ATTACHMENTS_CONV_DEFAULT == 0);

/* Return >=0 if file denotes a valid message number */
static int a_attachments_is_msg(char const *file);

/* Fill in some basic attachment fields */
static struct mx_attachment *a_attachments_setup_base(
      struct mx_attachment *ap, char const *file);

/* Setup ap to point to a message */
static struct mx_attachment *a_attachments_setup_msg(struct mx_attachment *ap,
      char const *msgcp, int msgno);

/* Try to create temporary charset converted version */
#ifdef mx_HAVE_ICONV
static boole a_attachments_iconv(struct mx_attachment *ap, FILE *ifp);
#endif

/* */
static void a_attachments_yay(struct mx_attachment const *ap);

static int
a_attachments_is_msg(char const *file){
   int rv;
   NYD2_IN;

   rv = -1;

   if(file[0] == '#'){
      uz ib;

      /* TODO Message numbers should be uz, and 0 may be a valid one */
      if(file[2] == '\0' && file[1] == '.'){
         if(dot != NIL)
            rv = (int)P2UZ(dot - message + 1);
      }else if((su_idec_uz_cp(&ib, &file[1], 10, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED || ib == 0 ||
            UCMP(z, ib, >, msgCount))
         rv = -1;
      else
         rv = S(int,ib);
   }

   NYD2_OU;
   return rv;
}

static struct mx_attachment *
a_attachments_setup_base(struct mx_attachment *ap, char const *file){
   NYD2_IN;

   ap->a_input_charset = ap->a_charset = NIL;
   ap->a_path_user = ap->a_path = ap->a_path_bname = ap->a_name = file;
   if((file = su_cs_rfind_c(file, '/')) != NIL)
      ap->a_path_bname = ap->a_name = ++file;
   else
      file = ap->a_name;
   ap->a_content_type = mx_mime_type_classify_filename(file);
   ap->a_content_disposition = "attachment";
   ap->a_content_description = NIL;
   ap->a_content_id = NIL;

   NYD2_OU;
   return ap;
}

static struct mx_attachment *
a_attachments_setup_msg(struct mx_attachment *ap, char const *msgcp,
      int msgno){
   NYD2_IN;

   ap->a_path_user = ap->a_path = ap->a_path_bname = ap->a_name = msgcp;
   ap->a_msgno = msgno;
   ap->a_content_type =
   ap->a_content_description =
   ap->a_content_disposition = NIL;
   ap->a_content_id = NIL;

   NYD2_OU;
   return ap;
}

#ifdef mx_HAVE_ICONV
static boole
a_attachments_iconv(struct mx_attachment *ap, FILE *ifp){
   struct str oul = {NIL, 0}, inl = {NIL, 0};
   uz cnt, lbsize;
   iconv_t icp;
   FILE *ofp;
   NYD_IN;

   hold_sigs(); /* TODO until we have signal manager (see TODO) */

   ofp = NIL;

   icp = n_iconv_open(ap->a_charset, ap->a_input_charset);
   if(icp == R(iconv_t,-1)){
      s32 eno;

      if((eno = su_err_no()) == su_ERR_INVAL)
         goto jeconv;
      else
         n_perr(_("iconv_open"), eno);
      goto jerr;
   }

   cnt = S(uz,fsize(ifp));

   if((ofp = mx_fs_tmp_open("atticonv", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL){
      n_perr(_("Temporary attachment data file"), 0);
      goto jerr;
   }

   for(lbsize = 0;;){
      if(fgetline(&inl.s, &lbsize, &cnt, &inl.l, ifp, FAL0) == NIL){
         if(!cnt || feof(ifp))
            break;
         n_perr(_("I/O read error occurred"), 0);
         goto jerr;
      }

      if(n_iconv_str(icp, n_ICONV_IGN_NOREVERSE, &oul, &inl, NIL) != 0)
         goto jeconv;
      if((inl.l = fwrite(oul.s, sizeof *oul.s, oul.l, ofp)) != oul.l){
         n_perr(_("I/O write error occurred"), 0);
         goto jerr;
      }
   }
   fflush_rewind(ofp);

   ap->a_tmpf = ofp;
jleave:
   if(inl.s != NIL)
      su_FREE(inl.s);
   if(oul.s != NIL)
      su_FREE(oul.s);
   if(icp != R(iconv_t,-1))
      n_iconv_close(icp);

   mx_fs_close(ifp);

   rele_sigs(); /* TODO until we have signal manager (see TODO) */
   NYD_OU;
   return (ofp != NIL);

jeconv:
   n_err(_("Cannot convert from %s to %s\n"),
      ap->a_input_charset, ap->a_charset);
jerr:
   if(ofp != NIL)
      mx_fs_close(ofp);
   ofp = NIL;
   goto jleave;
}
#endif /* mx_HAVE_ICONV */

static void
a_attachments_yay(struct mx_attachment const *ap){
   NYD2_IN;

   if(ap->a_msgno > 0)
      fprintf(n_stdout, _("Added message/rfc822 attachment for message #%d\n"),
         ap->a_msgno);
   else
      fprintf(n_stdout, _("Added attachment %s (%s)\n"),
         n_shexp_quote_cp(ap->a_name, FAL0),
         n_shexp_quote_cp(ap->a_path_user, FAL0));

   NYD2_OU;
}

struct mx_attachment *
mx_attachments_append(struct mx_attachment *aplist, char const *file,
      BITENUM_IS(u32,mx_attachments_error) *aerr_or_nil,
      struct mx_attachment **newap_or_nil){
#ifdef mx_HAVE_ICONV
   FILE *cnvfp;
#endif
   int msgno;
   char const *file_user, *incs, *oucs;
   struct mx_attachment *nap, *ap;
   enum mx_attachments_error aerr;
   NYD_IN;

#ifdef mx_HAVE_ICONV
   cnvfp = NIL;
#endif
   aerr = mx_ATTACHMENTS_ERR_NONE;
   nap = NIL;
   incs = oucs = NIL;

   if(*file == '\0'){
      aerr = mx_ATTACHMENTS_ERR_OTHER;
      goto jleave;
   }
   file_user = savestr(file); /* TODO recreate after fexpand()!?! */

   if((msgno = a_attachments_is_msg(file)) < 0){
      int e;
      char const *cp, *ncp;

jrefexp:
      if((file = fexpand(file, (FEXP_NONE | FEXP_LOCAL_FILE))) == NIL){
         aerr = mx_ATTACHMENTS_ERR_OTHER;
         goto jleave;
      }

#ifndef mx_HAVE_ICONV
      if(oucs != NIL && oucs != (char*)-1){
         n_err(_("No iconv support, cannot do %s\n"),
            n_shexp_quote_cp(file_user, FAL0));
         aerr = mx_ATTACHMENTS_ERR_ICONV_NAVAIL;
         goto jleave;
      }
#endif

      if((
#ifdef mx_HAVE_ICONV
            (oucs != NIL && oucs != R(char*,-1))
               ? (cnvfp = mx_fs_open(file, "r")) == NIL :
#endif
               access(file, R_OK) != 0)){
         e = su_err_no();

         /* It may not have worked because of a character-set specification,
          * so try to extract that and retry once */
         if(incs == NIL && (cp = su_cs_rfind_c(file, '=')) != NIL){
            uz i;
            char *nfp, c;

            nfp = savestrbuf(file, P2UZ(cp - file));

            for(ncp = ++cp; (c = *cp) != '\0'; ++cp)
               if(!su_cs_is_alnum(c) && !su_cs_is_punct(c))
                  break;
               else if(c == '#'){
                  if(incs == NIL){
                     i = P2UZ(cp - ncp);
                     if(i == 0 || (i == 1 && ncp[0] == '-'))
                        incs = (char*)-1;
                     else if((incs = n_iconv_normalize_name(savestrbuf(ncp, i))
                           ) == NIL){
                        e = su_ERR_INVAL;
                        goto jerr_fopen;
                     }
                     ncp = &cp[1];
                  }else
                     break;
               }
            if(c == '\0'){
               char *xp;

               i = P2UZ(cp - ncp);
               if(i == 0 || (i == 1 && ncp[0] == '-'))
                  xp = (char*)-1;
               else if((xp = n_iconv_normalize_name(savestrbuf(ncp, i))
                     ) == NIL){
                  e = su_ERR_INVAL;
                  goto jerr_fopen;
               }
               if(incs == NIL)
                  incs = xp;
               else
                  oucs = xp;
               file = nfp;
               goto jrefexp;
            }
         }

jerr_fopen:
         n_err(_("Failed to access attachment %s: %s\n"),
            n_shexp_quote_cp(file, FAL0), su_err_doc(e));
         aerr = mx_ATTACHMENTS_ERR_FILE_OPEN;
         goto jleave;
      }
   }

   nap = a_attachments_setup_base(su_AUTO_CALLOC(sizeof *nap), file);
   nap->a_path_user = file_user;
   if(msgno >= 0)
      nap = a_attachments_setup_msg(nap, file, msgno);
   else{
      nap->a_input_charset = (incs == NIL || incs == (char*)-1)
            ? savestr(ok_vlook(ttycharset)) : incs;
#ifdef mx_HAVE_ICONV
      if(cnvfp != NIL){
         nap->a_charset = oucs;
         if(!a_attachments_iconv(nap, cnvfp)){
            nap = NIL;
            aerr = mx_ATTACHMENTS_ERR_ICONV_FAILED;
            goto jleave;
         }
         nap->a_conv = mx_ATTACHMENTS_CONV_TMPFILE;
      }else
#endif
            if(incs != NIL && oucs == NIL)
         nap->a_conv = mx_ATTACHMENTS_CONV_FIX_INCS;
      else
         nap->a_conv = mx_ATTACHMENTS_CONV_DEFAULT;
   }

   if(aplist != NIL){
      for(ap = aplist; ap->a_flink != NIL; ap = ap->a_flink)
         ;
      ap->a_flink = nap;
      nap->a_blink = ap;
   }else
      aplist = nap;

jleave:
   if(aerr_or_nil != NIL)
      *aerr_or_nil = aerr;
   if(newap_or_nil != NIL)
      *newap_or_nil = nap;

   NYD_OU;
   return aplist;
}

struct mx_attachment *
mx_attachments_append_list(struct mx_attachment *aplist, char const *names){
   struct str shin;
   struct n_string shou, *shoup;
   NYD_IN;

   shoup = n_string_creat_auto(&shou);

   for(shin.s = n_UNCONST(names), shin.l = UZ_MAX;;){
      struct mx_attachment *nap;
      BITENUM_IS(u32,n_shexp_state) shs;

      shs = n_shexp_parse_token((n_SHEXP_PARSE_TRUNC |
            n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_LOG |
            n_SHEXP_PARSE_IFS_ADD_COMMA | n_SHEXP_PARSE_IGNORE_EMPTY),
            shoup, &shin, NIL);
      if(shs & n_SHEXP_STATE_ERR_MASK)
         break;

      if(shs & n_SHEXP_STATE_OUTPUT){
         aplist = mx_attachments_append(aplist, n_string_cp(shoup), NIL, &nap);
         if(nap != NIL){
            if(n_psonce & n_PSO_INTERACTIVE)
               a_attachments_yay(nap);
         }
      }

      if(shs & n_SHEXP_STATE_STOP)
         break;
   }

   n_string_gut(shoup);

   NYD_OU;
   return aplist;
}

struct mx_attachment *
mx_attachments_remove(struct mx_attachment *aplist, struct mx_attachment *ap){
   struct mx_attachment *bap, *fap;
   NYD_IN;

#ifdef mx_HAVE_DEVEL
   for(bap = aplist; aplist != NIL && aplist != ap; aplist = aplist->a_flink)
      ;
   ASSERT(aplist != NIL);
   aplist = bap;
#endif

   if(ap == aplist){
      if((aplist = ap->a_flink) != NIL)
         aplist->a_blink = NIL;
   }else{
      bap = ap->a_blink;
      fap = ap->a_flink;
      if(bap != NIL)
         bap->a_flink = fap;
      if(fap != NIL)
         fap->a_blink = bap;
   }

   if(ap->a_conv == mx_ATTACHMENTS_CONV_TMPFILE)
      mx_fs_close(ap->a_tmpf);

   NYD_OU;
   return aplist;
}

struct mx_attachment *
mx_attachments_find(struct mx_attachment *aplist, char const *name,
      boole *stat_or_nil){
   int msgno;
   char const *bname;
   boole status, sym;
   struct mx_attachment *saved;
   NYD_IN;

   saved = NIL;
   status = FAL0;

   if((bname = su_cs_rfind_c(name, '/')) != NIL){
      for(++bname; aplist != NIL; aplist = aplist->a_flink)
         if(!su_cs_cmp(name, aplist->a_path)){
            status = TRU1;
            /* Exact match with path components: done */
            goto jleave;
         }else if(!su_cs_cmp(bname, aplist->a_path_bname)){
            if(!status){
               saved = aplist;
               status = TRU1;
            }else
               status = TRUM1;
         }
   }else if((msgno = a_attachments_is_msg(name)) < 0){
      for(sym = FAL0; aplist != NIL; aplist = aplist->a_flink){
         if(!su_cs_cmp(name, aplist->a_name)){
            if(!status || !sym){
               saved = aplist;
               sym = TRU1;
            }
         }else if(!su_cs_cmp(name, aplist->a_path_bname)){
            if(!status)
               saved = aplist;
         }else
            continue;
         status = status ? TRUM1 : TRU1;
      }
   }else{
      for(; aplist != NIL; aplist = aplist->a_flink){
         if(aplist->a_msgno > 0 && aplist->a_msgno == msgno){
            status = TRU1;
            goto jleave;
         }
      }
   }
   if(saved != NIL)
      aplist = saved;

jleave:
   if(stat_or_nil != NIL)
      *stat_or_nil = status;

   NYD_OU;
   return aplist;
}

struct mx_attachment *
mx_attachments_list_edit(struct mx_attachment *aplist,
      BITENUM_IS(u32,mx_go_input_flags) gif){
   /* Modify already present ones? Append some more? */
   char prefix[32];
   char const *inidat;
   struct n_string shou, *shoup;
   struct mx_attachment *naplist, *ap;
   u32 attno;
   NYD_IN;

   shoup = n_string_creat_auto(&shou);
   attno = 1;

   for(naplist = NIL;;){
      snprintf(prefix, sizeof prefix, A_("#%" PRIu32 " filename: "), attno);

      if(aplist != NIL){
         /* TODO If we would create .a_path_user in append() after any
          * TODO expansion then we could avoid closing+rebuilding the temporary
          * TODO file if the new user input matches the original value! */
         if(aplist->a_conv == mx_ATTACHMENTS_CONV_TMPFILE)
            mx_fs_close(aplist->a_tmpf);
         inidat = n_shexp_quote_cp(aplist->a_path_user, FAL0);
      }else
         inidat = su_empty;

      ap = NIL;
      if(mx_tty_getfilename(shoup, gif, prefix, inidat) != TRU1)
         break;

      naplist = mx_attachments_append(naplist, n_string_cp(shoup), NIL, &ap);
      if(ap != NIL){
         if(n_psonce & n_PSO_INTERACTIVE)
            a_attachments_yay(ap);
         ++attno;
      }

      if(aplist != NIL){
         aplist = aplist->a_flink;
         /* In non-interactive or batch mode an empty line ends processing */
         if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_BATCH_FLAG))
            continue;
      }
      if(ap == NIL)
         break;
   }

   NYD_OU;
   return naplist;
}

sz
mx_attachments_list_print(struct mx_attachment const *aplist, FILE *fp){
   struct mx_attachment const *ap;
   u32 attno;
   sz rv;
   NYD_IN;

   rv = 0;

   for(attno = 1, ap = aplist; ap != NIL; ++rv, ++attno, ap = ap->a_flink){
      if(ap->a_msgno > 0){
         if(fprintf(fp, "#%" PRIu32 ". message/rfc822: %d\n", attno,
               ap->a_msgno) < 0)
            goto jerr;
      }else{
         char const *incs, *oucs;

         if(!su_state_has(su_STATE_REPRODUCIBLE)){
            incs = ap->a_input_charset;
            oucs = ap->a_charset;
         }else
            incs = oucs = su_reproducible_build;

         if(fprintf(fp, "#%" PRIu32 ": %s [%s -- %s",
               attno, n_shexp_quote_cp(ap->a_name, FAL0),
               n_shexp_quote_cp(ap->a_path, FAL0),
               (ap->a_content_type != NIL
                ? ap->a_content_type : _("unclassified content"))) < 0)
            goto jerr;

         if(ap->a_conv == mx_ATTACHMENTS_CONV_TMPFILE){
            /* I18N: input and output character set as given */
            if(fprintf(fp, _(", incs=%s -> oucs=%s (readily converted)"),
                  incs, oucs) < 0)
               goto jerr;
         }else if(ap->a_conv == mx_ATTACHMENTS_CONV_FIX_INCS){
            /* I18N: input character set as given, no conversion to apply */
            if(fprintf(fp, _(", incs=%s (no conversion)"), incs) < 0)
               goto jerr;
         }else if(ap->a_conv == mx_ATTACHMENTS_CONV_DEFAULT){
            if(incs != NIL){
               /* I18N: input character set as given, output iterates */
               if(fprintf(fp, _(", incs=%s -> oucs=*sendcharsets*"), incs) < 0)
                  goto jerr;
            }else if(ap->a_content_type == NIL ||
                  !su_cs_cmp_case_n(ap->a_content_type, "text/", 5)){
               if(fprintf(fp, _(", default character set handling")) < 0)
                  goto jerr;
            }
         }

         if(putc(']', fp) == EOF || putc('\n', fp) == EOF)
            goto jerr;
      }
   }

jleave:
   NYD_OU;
   return rv;
jerr:
   rv = -1;
   goto jleave;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_ATTACHMENTS
/* s-it-mode */
