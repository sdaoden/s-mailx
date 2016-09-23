/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of attachments.
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
#define n_FILE attachments

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* We use calloc() for struct attachment */
CTA(AC_DEFAULT == 0);

/* Fill in some attachment fields; don't be interactive if number==0 */
static struct attachment * _fill_in(enum n_lexinput_flags lif,
                              struct attachment *ap, char const *file,
                              ui32_t number);

/* Ask the user to edit file names and other data for the given attachment */
static struct attachment * _read_attachment_data(enum n_lexinput_flags lif,
                              struct attachment *ap, ui32_t number);

/* Try to create temporary charset converted version */
#ifdef HAVE_ICONV
static int                 _attach_iconv(struct attachment *ap);
#endif

static struct attachment *
_fill_in(enum n_lexinput_flags lif, struct attachment *ap, char const *file,
      ui32_t number)
{
   /* XXX The "attachment-ask-content-*" variables are left undocumented
    * since "they are for RFC connoisseurs only" ;) */
   char prefix[80 * 2];
   NYD_ENTER;

   ap->a_input_charset = ap->a_charset = NULL;

   ap->a_name = file;
   if ((file = strrchr(file, '/')) != NULL)
      ++file;
   else
      file = ap->a_name;

   ap->a_content_type = mime_type_classify_filename(file);
   if (number > 0 && ok_blook(attachment_ask_content_type)) {
      snprintf(prefix, sizeof prefix, "#%-5" PRIu32 " Content-Type: ", number);
      ap->a_content_type = n_lex_input_cp(lif, prefix, ap->a_content_type);
   }

   if (number > 0 && ok_blook(attachment_ask_content_disposition)) {
      snprintf(prefix, sizeof prefix, "#%-5" PRIu32 " Content-Disposition: ",
         number);
      if ((ap->a_content_disposition = n_lex_input_cp(lif, prefix,
            ap->a_content_disposition)) == NULL)
         goto jcdis;
   } else
jcdis:
      ap->a_content_disposition = "attachment";

   if (number > 0 && ok_blook(attachment_ask_content_id)) {
      snprintf(prefix, sizeof prefix, "#%-5" PRIu32 " Content-ID: ", number);
      ap->a_content_id = n_lex_input_cp(lif, prefix, ap->a_content_id);
   } else
      ap->a_content_id = NULL;

   if (number > 0 && ok_blook(attachment_ask_content_description)) {
      snprintf(prefix, sizeof prefix, "#%-5" PRIu32 " Content-Description: ",
         number);
      ap->a_content_description = n_lex_input_cp(lif, prefix,
            ap->a_content_description);
   }
   NYD_LEAVE;
   return ap;
}

static sigjmp_buf    __atticonv_jmp; /* TODO oneday, we won't need it no more */
static int volatile  __atticonv_sig; /* TODO oneday, we won't need it no more */
static void
__atticonv_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __atticonv_sig = sig;
   siglongjmp(__atticonv_jmp, 1);
}

static struct attachment *
_read_attachment_data(enum n_lexinput_flags lif,
      struct attachment * volatile ap, ui32_t number)
{
   sighandler_type volatile ohdl;
   char prefix[80 * 2];
   struct str shin;
   struct n_string shou, *shoup;
   char const *cslc, *cp, *defcs;
   NYD_ENTER;

   UNINIT(cslc, NULL);
   shoup = n_string_creat(&shou);

   hold_sigs(); /* TODO until we have signal manager (see TODO) */
   ohdl = safe_signal(SIGINT, SIG_IGN);
   __atticonv_sig = 0;
   if (sigsetjmp(__atticonv_jmp, 1)) {
      ap = NULL;
      goto jleave;
   }
   safe_signal(SIGINT, &__atticonv_onsig);

   if (ap == NULL)
      ap = csalloc(1, sizeof *ap);
   else if (ap->a_msgno) {
      char *ecp = salloc(24);
      snprintf(ecp, 24, "#%" PRIu32, (ui32_t)ap->a_msgno);
      ap->a_msgno = 0;
      ap->a_content_description = NULL;
      ap->a_name = ecp;
   } else if (ap->a_conv == AC_TMPFILE) {
      Fclose(ap->a_tmpf);
      DBG( ap->a_tmpf = NULL; )
      ap->a_conv = AC_DEFAULT;
   }

   rele_sigs(); /* TODO until we have signal manager (see TODO) */
   snprintf(prefix, sizeof prefix, _("#%-5" PRIu32 " filename: "), number);
   for (;;) {
      if ((cp = ap->a_name) != NULL)
         cp = n_shexp_quote_cp(cp, FAL0);
      if ((cp = n_lex_input_cp(lif, prefix, cp)) == NULL) {
         ap->a_name = NULL;
         ap = NULL;
         goto jleave;
      }

      shin.s = UNCONST(cp);
      shin.l = UIZ_MAX;
      if((n_shexp_parse_token(shoup, &shin, n_SHEXP_PARSE_TRUNC |
               n_SHEXP_PARSE_TRIMSPACE | n_SHEXP_PARSE_LOG |
               n_SHEXP_PARSE_IGNORE_EMPTY) &
             (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_ERR_MASK)) !=
             n_SHEXP_STATE_OUTPUT)
         continue;
      if(shin.l != 0)
         continue;
      cp = n_string_cp(shoup);

      /* May be a message number (XXX add "AC_MSG", use that not .a_msgno) */
      if (cp[0] == '#') {
         char *ecp;
         int msgno = (int)strtol(cp + 1, &ecp, 10);

         if (msgno > 0 && msgno <= msgCount && *ecp == '\0') {
            ap->a_name = cp;
            ap->a_msgno = msgno;
            ap->a_content_type = ap->a_content_disposition =
                  ap->a_content_id = NULL;
            ap->a_content_description = _("Attached message content");
            if (options & OPT_INTERACTIVE)
               printf(_("~@: added message #%" PRIu32 "\n"), (ui32_t)msgno);
            goto jleave;
         }
      }

      if ((cp = fexpand(cp, FEXP_LOCAL | FEXP_NVAR)) != NULL &&
            !access(cp, R_OK)) {
         ap->a_name = cp;
         break;
      }
      n_perr(cp, 0);
   }

   ap = _fill_in(lif, ap, cp, number);

   /*
    * Character set of attachments: enum attach_conv
    */
   cslc = charset_get_lc();
#ifdef HAVE_ICONV
   if (!(options & OPT_INTERACTIVE))
      goto jcs;
   if ((cp = ap->a_content_type) != NULL && ascncasecmp(cp, "text/", 5) != 0 &&
         !getapproval(_("File doesn't indicate text content, "
            "edit character sets"), TRU1)) {
      ap->a_conv = AC_DEFAULT;
      goto jleave;
   }

jcs_restart:
   charset_iter_reset(NULL);
jcs:
#endif
   snprintf(prefix, sizeof prefix, _("#%-5" PRIu32 " input charset: "),
      number);
   if ((defcs = ap->a_input_charset) == NULL)
      defcs = cslc;
   cp = ap->a_input_charset = n_lex_input_cp(lif, prefix, defcs);
#ifdef HAVE_ICONV
   if (!(options & OPT_INTERACTIVE)) {
#endif
      ap->a_conv = (cp != NULL) ? AC_FIX_INCS : AC_DEFAULT;
#ifdef HAVE_ICONV
      goto jleave;
   }

   snprintf(prefix, sizeof prefix,
      _("#%-5" PRIu32 " output (send) charset: "), number);
   if ((defcs = ap->a_charset) == NULL)
      defcs = charset_iter();
   defcs = ap->a_charset = n_lex_input_cp(lif, prefix, defcs);

   /* Input, no output -> input=as given, output=no conversion at all */
   if (cp != NULL && defcs == NULL) {
      ap->a_conv = AC_FIX_INCS;
      goto jdone;
   }

   /* No input, no output -> input=*ttycharset*, output=iterator */
   if (cp == NULL && defcs == NULL) {
      ap->a_conv = AC_DEFAULT;
      ap->a_input_charset = cslc;
      ap->a_charset = charset_iter();
      assert(charset_iter_is_valid());
      charset_iter_next();
   }
   /* No input, output -> input=*ttycharset*, output=as given */
   else if (cp == NULL && defcs != NULL) {
      ap->a_conv = AC_FIX_OUTCS;
      ap->a_input_charset = cslc;
   }
   /* Input, output -> try conversion from input=as given to output=as given */

   printf(_("Trying conversion from %s to %s\n"), ap->a_input_charset,
      ap->a_charset);
   if (_attach_iconv(ap))
      ap->a_conv = AC_TMPFILE;
   else {
      ap->a_conv = AC_DEFAULT;
      ap->a_input_charset = cp;
      ap->a_charset = defcs;
      if (!charset_iter_is_valid()) {
         printf(_("*sendcharsets* and *charset-8bit* iteration "
            "exhausted, restarting\n"));
         goto jcs_restart;
      }
      goto jcs;
   }
jdone:
#endif
   if (options & OPT_INTERACTIVE)
      printf(_("Added attachment %s\n"), n_shexp_quote_cp(ap->a_name, FAL0));
jleave:
   n_string_gut(shoup);

   safe_signal(SIGINT, ohdl);/* TODO until we have signal manager (see TODO) */
   if (__atticonv_sig != 0) {
      sigset_t nset;
      sigemptyset(&nset);
      sigaddset(&nset, SIGINT);
      sigprocmask(SIG_UNBLOCK, &nset, NULL);
      /* Caller kills */
   }
   NYD_LEAVE;
   return ap;
}

#ifdef HAVE_ICONV
static int
_attach_iconv(struct attachment *ap)
{
   struct str oul = {NULL, 0}, inl = {NULL, 0};
   FILE *fo = NULL, *fi = NULL;
   size_t cnt, lbsize;
   iconv_t icp;
   NYD_ENTER;

   hold_sigs(); /* TODO until we have signal manager (see TODO) */

   icp = n_iconv_open(ap->a_charset, ap->a_input_charset);
   if (icp == (iconv_t)-1) {
      if (errno == EINVAL)
         goto jeconv;
      else
         n_perr(_("iconv_open"), 0);
      goto jerr;
   }

   if ((fi = Fopen(ap->a_name, "r")) == NULL) {
      n_perr(ap->a_name, 0);
      goto jerr;
   }
   cnt = (size_t)fsize(fi);

   if ((fo = Ftmp(NULL, "aticonv", OF_RDWR | OF_UNLINK | OF_REGISTER)) ==
         NULL) {
      n_perr(_("temporary mail file"), 0);
      goto jerr;
   }

   for (lbsize = 0;;) {
      if (fgetline(&inl.s, &lbsize, &cnt, &inl.l, fi, 0) == NULL) {
         if (!cnt)
            break;
         n_perr(_("I/O read error occurred"), 0);
         goto jerr;
      }

      if (n_iconv_str(icp, &oul, &inl, NULL, FAL0) != 0)
         goto jeconv;
      if ((inl.l = fwrite(oul.s, sizeof *oul.s, oul.l, fo)) != oul.l) {
         n_perr(_("I/O write error occurred"), 0);
         goto jerr;
      }
   }
   fflush_rewind(fo);

   ap->a_tmpf = fo;
jleave:
   if (inl.s != NULL)
      free(inl.s);
   if (oul.s != NULL)
      free(oul.s);
   if (fi != NULL)
      Fclose(fi);
   if (icp != (iconv_t)-1)
      n_iconv_close(icp);

   rele_sigs(); /* TODO until we have signal manager (see TODO) */
   NYD_LEAVE;
   return (fo != NULL);

jeconv:
   n_err(_("Cannot convert from %s to %s\n"),
      ap->a_input_charset, ap->a_charset);
jerr:
   if (fo != NULL)
      Fclose(fo);
   fo = NULL;
   goto jleave;
}
#endif /* HAVE_ICONV */

/* TODO add_attachment(): also work with **aphead, not *aphead ... */
FL struct attachment *
add_attachment(enum n_lexinput_flags lif, struct attachment *aphead,
      char *file, struct attachment **newap)
{
   struct attachment *nap = NULL, *ap;
   NYD_ENTER;

   if ((file = fexpand(file, FEXP_LOCAL | FEXP_NVAR)) == NULL)
      goto jleave;
   if (access(file, R_OK) != 0)
      goto jleave;

   nap = _fill_in(lif, csalloc(1, sizeof *nap), file, 0);
   if (newap != NULL)
      *newap = nap;
   if (aphead != NULL) {
      for (ap = aphead; ap->a_flink != NULL; ap = ap->a_flink)
         ;
      ap->a_flink = nap;
      nap->a_blink = ap;
   } else {
      nap->a_blink = NULL;
      aphead = nap;
   }
   nap = aphead;
jleave:
   NYD_LEAVE;
   return nap;
}

FL void
append_attachments(enum n_lexinput_flags lif, struct attachment **aphead,
      char *names){
   struct str shin;
   struct n_string shou, *shoup;
   NYD_ENTER;

   shoup = n_string_creat_auto(&shou);

   for(shin.s = names, shin.l = UIZ_MAX;;){
      struct attachment *xaph, *nap;
      enum n_shexp_state shs;

      shs = n_shexp_parse_token(shoup, &shin, n_SHEXP_PARSE_TRUNC |
             n_SHEXP_PARSE_TRIMSPACE | n_SHEXP_PARSE_LOG |
             n_SHEXP_PARSE_IFS_ADD_COMMA | n_SHEXP_PARSE_IGNORE_EMPTY);
      if(shs & n_SHEXP_STATE_ERR_MASK)
         break;

      if(shs & n_SHEXP_STATE_OUTPUT){
         if((xaph = add_attachment(lif, *aphead, n_string_cp(shoup), &nap)
               ) != NULL){
            *aphead = xaph;
            if(options & OPT_INTERACTIVE)
               printf(_("Added attachment %s\n"),
                  n_shexp_quote_cp(nap->a_name, FAL0));
         }else
            n_perr(n_string_cp(shoup), 0);
      }

      if(shs & n_SHEXP_STATE_STOP)
         break;
   }
   n_string_gut(shoup);
   NYD_LEAVE;
}

FL void
edit_attachments(enum n_lexinput_flags lif, struct attachment **aphead)
{
   struct attachment *ap, *fap, *bap;
   ui32_t attno = 1;
   NYD_ENTER;

   /* C99 */{
      static bool_t note_given; /* v15-compat: DROP */

      if(!note_given){
         note_given = TRU1;
         printf(_("# Only supports sh(1)ell-style quoting for file names\n"));
      }
   }

   /* Modify already present ones? */
   for (ap = *aphead; ap != NULL; ap = fap) {
      if (_read_attachment_data(lif, ap, attno) != NULL) {
         fap = ap->a_flink;
         ++attno;
         continue;
      }
      fap = ap->a_flink;
      if ((bap = ap->a_blink) != NULL)
         bap->a_flink = fap;
      else
         *aphead = fap;
      if (fap != NULL)
         fap->a_blink = bap;
      /*else*//* TODO until we have signal manager (see TODO) */
      if (__atticonv_sig != 0)
         n_raise(SIGINT);
      if (fap == NULL)
         goto jleave;
   }

   /* Add some more? */
   if ((bap = *aphead) != NULL)
      while (bap->a_flink != NULL)
         bap = bap->a_flink;
   while ((fap = _read_attachment_data(lif, NULL, attno)) != NULL) {
      if (bap != NULL)
         bap->a_flink = fap;
      else
         *aphead = fap;
      fap->a_blink = bap;
      fap->a_flink = NULL;
      bap = fap;
      ++attno;
   }
   if (__atticonv_sig != 0) /* TODO until we have signal manager (see TODO) */
      n_raise(SIGINT);
jleave:
   NYD_LEAVE;
}

/* s-it-mode */
