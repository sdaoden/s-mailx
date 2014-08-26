/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Handling of attachments.
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

/* We use calloc() for struct attachment */
CTA(AC_DEFAULT == 0);

/* Fill in some attachment fields; don't be interactive if number==0n */
static struct attachment * _fill_in(struct attachment *ap,
                              char const *file, ui32_t number);

/* Ask the user to edit file names and other data for the given attachment */
static struct attachment * _read_attachment_data(struct attachment *ap,
                              ui32_t number);

/* Try to create temporary charset converted version */
#ifdef HAVE_ICONV
static int                 _attach_iconv(struct attachment *ap);
#endif

static struct attachment *
_fill_in(struct attachment *ap, char const *file, ui32_t number)
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

   ap->a_content_type = mime_classify_content_type_by_fileext(file);
   if (number > 0 && ok_blook(attachment_ask_content_type)) {
      snprintf(prefix, sizeof prefix, "#%u\tContent-Type: ", number);
      ap->a_content_type = readstr_input(prefix, ap->a_content_type);
   }

   if (number > 0 && ok_blook(attachment_ask_content_disposition)) {
      snprintf(prefix, sizeof prefix, "#%u\tContent-Disposition: ", number);
      if ((ap->a_content_disposition = readstr_input(prefix,
            ap->a_content_disposition)) == NULL)
         goto jcdis;
   } else
jcdis:
      ap->a_content_disposition = "attachment";

   if (number > 0 && ok_blook(attachment_ask_content_id)) {
      snprintf(prefix, sizeof prefix, "#%u\tContent-ID: ", number);
      ap->a_content_id = readstr_input(prefix, ap->a_content_id);
   } else
      ap->a_content_id = NULL;

   if (number > 0 && ok_blook(attachment_ask_content_description)) {
      snprintf(prefix, sizeof prefix, "#%u\tContent-Description: ", number);
      ap->a_content_description = readstr_input(prefix,
            ap->a_content_description);
   }
   NYD_LEAVE;
   return ap;
}

static sigjmp_buf __atticonv_jmp; /* TODO someday, we won't need it no more */
static int __atticonv_sig; /* TODO someday, we won't need it no more */
static void
__atticonv_onsig(int sig) /* TODO someday, we won't need it no more */
{
   NYD_X; /* Signal handler */
   __atticonv_sig = sig;
   siglongjmp(__atticonv_jmp, 1);
}

static struct attachment *
_read_attachment_data(struct attachment * volatile ap, ui32_t number)
{
   sighandler_type volatile ohdl;
   char prefix[80 * 2];
   char const *cslc = NULL/*cc uninit?*/, *cp, *defcs;
   NYD_ENTER;

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
   snprintf(prefix, sizeof prefix, _("#%" PRIu32 "\tfilename: "), number);
   for (;;) {
      if ((ap->a_name = readstr_input(prefix, ap->a_name)) == NULL) {
         ap = NULL;
         goto jleave;
      }

      /* May be a message number (XXX add "AC_MSG", use that not .a_msgno) */
      if (ap->a_name[0] == '#') {
         char *ecp;
         int msgno = (int)strtol(ap->a_name + 1, &ecp, 10);
         if (msgno > 0 && msgno <= msgCount && *ecp == '\0') {
            ap->a_msgno = msgno;
            ap->a_content_type = ap->a_content_disposition =
                  ap->a_content_id = NULL;
            ap->a_content_description = _("Attached message content");
            if (options & OPT_INTERACTIVE)
               printf(_("~@: added message #%" PRIu32 "\n"), (ui32_t)msgno);
            goto jleave;
         }
      }

      if ((cp = file_expand(ap->a_name)) != NULL && !access(cp, R_OK)) {
         ap->a_name = cp;
         break;
      }
      perror(cp);
   }

   ap = _fill_in(ap, cp, number);

   /*
    * Character set of attachments: enum attach_conv
    */
   cslc = charset_get_lc();
#ifdef HAVE_ICONV
   if (!(options & OPT_INTERACTIVE))
      goto jcs;
   if ((cp = ap->a_content_type) != NULL && ascncasecmp(cp, "text/", 5) != 0 &&
         !getapproval(_("Filename doesn't indicate text content - "
            "edit charsets nonetheless? "), TRU1)) {
      ap->a_conv = AC_DEFAULT;
      goto jleave;
   }

jcs_restart:
   charset_iter_reset(NULL);
jcs:
#endif
   snprintf(prefix, sizeof prefix, _("#%" PRIu32 "\tinput charset: "),
      number);
   if ((defcs = ap->a_input_charset) == NULL)
      defcs = cslc;
   cp = ap->a_input_charset = readstr_input(prefix, defcs);
#ifdef HAVE_ICONV
   if (!(options & OPT_INTERACTIVE)) {
#endif
      ap->a_conv = (cp != NULL) ? AC_FIX_INCS : AC_DEFAULT;
#ifdef HAVE_ICONV
      goto jleave;
   }

   snprintf(prefix, sizeof prefix,
      _("#%" PRIu32 "\toutput (send) charset: "), number);
   if ((defcs = ap->a_charset) == NULL)
      defcs = charset_iter();
   defcs = ap->a_charset = readstr_input(prefix, defcs);

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
      printf(_("~@: added attachment \"%s\"\n"), ap->a_name);
jleave:
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
         perror("iconv_open");
      goto jerr;
   }

   if ((fi = Fopen(ap->a_name, "r")) == NULL) {
      perror(ap->a_name);
      goto jerr;
   }
   cnt = fsize(fi);

   if ((fo = Ftmp(NULL, "atic", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      perror(_("temporary mail file"));
      goto jerr;
   }

   for (lbsize = 0;;) {
      if (fgetline(&inl.s, &lbsize, &cnt, &inl.l, fi, 0) == NULL) {
         if (!cnt)
            break;
         perror(_("I/O read error occurred"));
         goto jerr;
      }

      if (n_iconv_str(icp, &oul, &inl, NULL, FAL0) != 0)
         goto jeconv;
      if ((inl.l = fwrite(oul.s, sizeof *oul.s, oul.l, fo)) != oul.l) {
         perror(_("I/O write error occurred"));
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
   fprintf(stderr, _("Cannot convert from %s to %s\n"),
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
add_attachment(struct attachment *aphead, char *file, struct attachment **newap)
{
   struct attachment *nap = NULL, *ap;
   NYD_ENTER;

   if ((file = file_expand(file)) == NULL)
      goto jleave;
   if (access(file, R_OK) != 0)
      goto jleave;

   nap = _fill_in(csalloc(1, sizeof *nap), file, 0);
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
append_attachments(struct attachment **aphead, char *names)
{
   char *cp;
   struct attachment *xaph, *nap;
   NYD_ENTER;

   while ((cp = n_strsep(&names, ',', 1)) != NULL) {
      if ((xaph = add_attachment(*aphead, cp, &nap)) != NULL) {
         *aphead = xaph;
         if (options & OPT_INTERACTIVE)
            printf(_("~@: added attachment \"%s\"\n"), nap->a_name);
      } else
         perror(cp);
   }
   NYD_LEAVE;
}

FL void
edit_attachments(struct attachment **aphead)
{
   struct attachment *ap, *fap, *bap;
   ui32_t attno = 1;
   NYD_ENTER;

   /* Modify already present ones? */
   for (ap = *aphead; ap != NULL; ap = fap) {
      if (_read_attachment_data(ap, attno) != NULL) {
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
         kill(0, SIGINT);
      if (fap == NULL)
         goto jleave;
   }

   /* Add some more? */
   if ((bap = *aphead) != NULL)
      while (bap->a_flink != NULL)
         bap = bap->a_flink;
   while ((fap = _read_attachment_data(NULL, attno)) != NULL) {
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
      kill(0, SIGINT);
jleave:
   NYD_LEAVE;
}

/* s-it-mode */
