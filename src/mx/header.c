/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Routines for processing and detecting headlines.
 *@ TODO Mostly a hackery, we need RFC compliant parsers instead.
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
#define su_FILE header
#define mx_SOURCE
#define mx_SOURCE_HEADER

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/cmd.h"
#include "mx/compat.h"
#include "mx/file-streams.h"
#include "mx/mime.h"
#include "mx/mime-probe.h"
#include "mx/names.h"
#include "mx/srch-ctx.h"
#include "mx/time.h"
#include "mx/ui-str.h"
#include "mx/url.h"

#ifdef mx_HAVE_COLOUR
# include "mx/colour.h"
#endif

/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

struct a_header_cmatch_data{
   u32 hcmd_len_x;      /* Length of .tdata,.. */
   u32 hcmd_len_min;    /* ..less all optional entries */
   char const *hcmd_data;  /* Template date - see a_header_cmatch_data[] */
};

/* Template characters for cmatch_data.tdata:
 * 'A'   An upper case char
 * 'a'   A lower case char
 * ' '   A space
 * '0'   A digit
 * 'O'   An optional digit or space; MUST be followed by '0space'!
 * ':'   A colon
 * '+'  Either a plus or a minus sign */
static struct a_header_cmatch_data const a_header_cmatch_data[] = {
   {24, 23, "Aaa Aaa O0 00:00:00 0000"},     /* BSD/ISO C90 ctime */
   {28, 27, "Aaa Aaa O0 00:00:00 AAA 0000"}, /* BSD tmz */
   {21, 20, "Aaa Aaa O0 00:00 0000"},        /* SysV ctime */
   {25, 24, "Aaa Aaa O0 00:00 AAA 0000"},    /* SysV tmz */
   /* RFC 822-alike From_ lines do not conform to RFC 4155, but seem to be used
    * in the wild (by UW-imap) */
   {30, 29, "Aaa Aaa O0 00:00:00 0000 +0000"},
   /* RFC 822 with zone spec; 1. military, 2. UT, 3. north america time
    * zone strings; note that 1. is strictly speaking not correct as some
    * letters are not used, and 2. is not because only "UT" is defined */
#define __reuse "Aaa Aaa O0 00:00:00 0000 AAA"
   {28 - 2, 27 - 2, __reuse},
   {28 - 1, 27 - 1, __reuse},
   {28 - 0, 27 - 0, __reuse},
   {0, 0, NULL}
};
#define a_HEADER_DATE_MINLEN 20
CTAV(n_FROM_DATEBUF > sizeof("From_") -1 + 3 + 30 +1);

/* Savage extract date field from From_ line.  linelen is convenience as line
 * must be terminated (but it may end in a newline [sequence]).
 * Return whether the From_ line was parsed successfully (-1 if the From_ line
 * wasn't really RFC 4155 compliant) */
static int a_header_extract_date_from_from_(char const *line, uz linelen,
            char datebuf[n_FROM_DATEBUF]);

/* Skip over "word" as found in From_ line */
static char const *a_header__from_skipword(char const *wp);

/* Match the date string against the date template (tp), return if match.
 * See a_header_cmatch_data[] for template character description */
static boole a_header_cmatch(char const *tp, char const *date);

/* Check whether date is a valid 'From_' date.
 * (Rather ctime(3) generated dates, according to RFC 4155) */
static boole a_header_is_date(char const *date);

/* ... And place the extracted date in `date' */
static void a_header_parse_from_(struct message *mp,
      char date[n_FROM_DATEBUF]);

/* xxx as long as we cannot grasp complete MIME mails via -t or compose mode,
 * xxx and selectively add onto what is missing / etc, 'need to ignore some */
static char const *a_header_extract_ignore_field_XXX(char const *linebuf);

/* Return the next header field found in the given message.
 * Return >= 0 if something found, < 0 elsewise.
 * "colon" is set to point to the colon in the header.
 * Must deal with \ continuations & other such fraud */
static long a_gethfield(enum n_header_extract_flags hef, FILE *f,
               char **linebuf, uz *linesize, long rem, char **colon);

static char const *a_header_date_token(char const *cp);

static int
a_header_extract_date_from_from_(char const *line, uz linelen,
   char datebuf[n_FROM_DATEBUF])
{
   int rv;
   char const *cp = line;
   NYD_IN;

   rv = 1;

   /* "From " */
   cp = a_header__from_skipword(cp);
   if (cp == NULL)
      goto jerr;
   /* "addr-spec " */
   cp = a_header__from_skipword(cp);
   if (cp == NULL)
      goto jerr;
   if((cp[0] == 't' || cp[0] == 'T') && (cp[1] == 't' || cp[1] == 'T') &&
         (cp[2] == 'y' || cp[2] == 'Y')){
      cp = a_header__from_skipword(cp);
      if (cp == NULL)
         goto jerr;
   }
   /* It seems there are invalid MBOX archives in the wild, compare
    * . http://bugs.debian.org/624111
    * . [Mutt] #3868: mutt should error if the imported mailbox is invalid
    * What they do is that they obfuscate the address to "name at host",
    * and even "name at host dot dom dot dom.
    * The [Aa][Tt] is also RFC 733, so be tolerant */
   else if((cp[0] == 'a' || cp[0] == 'A') && (cp[1] == 't' || cp[1] == 'T') &&
         cp[2] == ' '){
      rv = -1;
      cp += 3;
jat_dot:
      cp = a_header__from_skipword(cp);
      if (cp == NULL)
         goto jerr;
      if((cp[0] == 'd' || cp[0] == 'D') && (cp[1] == 'o' || cp[1] == 'O') &&
            (cp[2] == 't' || cp[2] == 'T') && cp[3] == ' '){
         cp += 4;
         goto jat_dot;
      }
   }

   linelen -= P2UZ(cp - line);
   if (linelen < a_HEADER_DATE_MINLEN)
      goto jerr;
   if (cp[linelen - 1] == '\n') {
      --linelen;
      /* (Rather IMAP/POP3 only) */
      if (cp[linelen - 1] == '\r')
         --linelen;
      if (linelen < a_HEADER_DATE_MINLEN)
         goto jerr;
   }
   if (linelen >= n_FROM_DATEBUF)
      goto jerr;

jleave:
   su_mem_copy(datebuf, cp, linelen);
   datebuf[linelen] = '\0';
   NYD_OU;
   return rv;
jerr:
   cp = _("<Unknown date>");
   linelen = su_cs_len(cp);
   if(linelen >= n_FROM_DATEBUF)
      linelen = n_FROM_DATEBUF -1;
   rv = 0;
   goto jleave;
}

static char const *
a_header__from_skipword(char const *wp)
{
   char c = 0;
   NYD2_IN;

   if (wp != NULL) {
      while ((c = *wp++) != '\0' && !su_cs_is_blank(c)) {
         if (c == '"') {
            while ((c = *wp++) != '\0' && c != '"')
               ;
            if (c != '"')
               --wp;
         }
      }
      for (; su_cs_is_blank(c); c = *wp++)
         ;
   }
   NYD2_OU;
   return (c == 0 ? NULL : wp - 1);
}

static boole
a_header_cmatch(char const *tp, char const *date){
   boole rv;
   char tc, dc;
   NYD2_IN;

   for(;;){
      tc = *tp++;
      dc = *date++;
      if((rv = (tc == '\0' && dc == '\0')))
         break; /* goto jleave; */

      switch(tc){
      case 'a':
         if(!su_cs_is_lower(dc))
            goto jleave;
         break;
      case 'A':
         if(!su_cs_is_upper(dc))
            goto jleave;
         break;
      case ' ':
         if(dc != ' ')
            goto jleave;
         break;
      case '0':
         if(!su_cs_is_digit(dc))
            goto jleave;
         break;
      case 'O':
         if(!su_cs_is_digit(dc) && dc != ' ')
               goto jleave;
         /*tc = *tp++*/ ++tp; /* is "0"! */
         dc = *date;
         if(su_cs_is_digit(dc))
            ++date;
         break;
      case ':':
         if(dc != ':')
            goto jleave;
         break;
      case '+':
         if(dc != '+' && dc != '-')
            goto jleave;
         break;
      }
   }
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_header_is_date(char const *date){
   struct a_header_cmatch_data const *hcmdp;
   uz dl;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   if((dl = su_cs_len(date)) >= a_HEADER_DATE_MINLEN)
      for(hcmdp = a_header_cmatch_data; hcmdp->hcmd_data != NULL; ++hcmdp)
         if(dl >= hcmdp->hcmd_len_min && dl <= hcmdp->hcmd_len_x &&
               (rv = a_header_cmatch(hcmdp->hcmd_data, date)))
            break;
   NYD2_OU;
   return rv;
}

static void
a_header_parse_from_(struct message *mp, char date[n_FROM_DATEBUF]){
   FILE *ibuf;
   int hlen;
   char *hline;
   uz hsize;
   NYD2_IN;

   mx_fs_linepool_aquire(&hline, &hsize);

   if((ibuf = setinput(&mb, mp, NEED_HEADER)) != NULL &&
         (hlen = readline_restart(ibuf, &hline, &hsize, 0)) > 0)
      a_header_extract_date_from_from_(hline, hlen, date);

   mx_fs_linepool_release(hline, hsize);
   NYD2_OU;
}

static char const *
a_header_extract_ignore_field_XXX(char const *linebuf){
   static char const a_ifa[][sizeof("Content-Transfer-Encoding")] = {
      "MIME-Version", "Content-Type", "Content-Transfer-Encoding",
         "Content-Disposition", "Content-ID"
   };

   char const *ccp;
   uz i;
   NYD2_IN;

   for(i = 0; i < NELEM(a_ifa); ++i)
      if(n_header_get_field(linebuf, ccp = a_ifa[i], NIL) != NIL)
         goto jleave;

   ccp = NIL;
jleave:
   NYD2_OU;
   return ccp;
}

static long
a_gethfield(enum n_header_extract_flags hef, FILE *f,
   char **linebuf, uz *linesize, long rem, char **colon)
{
   char *line2, *cp, *cp2;
   uz line2size;
   int c, isenc;
   NYD2_IN;

   if (*linebuf == NULL)
      *linebuf = n_realloc(*linebuf, *linesize = 1);
   **linebuf = '\0';

   mx_fs_linepool_aquire(&line2, &line2size);

   for (;;) {
      if (--rem < 0) {
         rem = -1;
         break;
      }
      if ((c = readline_restart(f, linebuf, linesize, 0)) <= 0) {
         rem = -1;
         break;
      }
      if((hef & n_HEADER_EXTRACT_IGNORE_SHELL_COMMENTS) && **linebuf == '#'){
         /* A comment may be last before body, too, ensure empty last line */
         **linebuf = '\0';
         continue;
      }

      for (cp = *linebuf; fieldnamechar(*cp); ++cp)
         ;
      if (cp > *linebuf)
         while (su_cs_is_blank(*cp))
            ++cp;
      if (cp == *linebuf) /* TODO very first line of input with lead WS? */
         continue;
      /* XXX Not a header line, logging only for -t / compose mode? */
      if(*cp != ':'){
         if(!(hef & n_HEADER_EXTRACT_IGNORE_FROM_) ||
               !is_head(*linebuf, c, FAL0))
            n_err(_("Not a header line, skipping: %s\n"),
               n_shexp_quote_cp(*linebuf, FAL0));
         continue;
      }

      /* I guess we got a headline.  Handle wraparound */
      *colon = cp;
      cp = *linebuf + c;
      for (;;) {
         isenc = 0;
         while (PCMP(--cp, >=, *linebuf) && su_cs_is_blank(*cp))
            ;
         cp++;
         if (rem <= 0)
            break;
         if (PCMP(cp - 8, >=, *linebuf) && cp[-1] == '=' && cp[-2] == '?')
            isenc |= 1;
         ungetc(c = getc(f), f);
         if (!su_cs_is_blank(c))
            break;
         c = readline_restart(f, &line2, &line2size, 0);
         if (c < 0)
            break;
         --rem;
         for (cp2 = line2; su_cs_is_blank(*cp2); ++cp2)
            ;
         c -= (int)P2UZ(cp2 - line2);
         if (cp2[0] == '=' && cp2[1] == '?' && c > 8)
            isenc |= 2;
         if (PCMP(cp + c, >=, *linebuf + *linesize - 2)) {
            uz diff = P2UZ(cp - *linebuf),
               colondiff = P2UZ(*colon - *linebuf);
            *linebuf = n_realloc(*linebuf, *linesize += c + 2);
            cp = &(*linebuf)[diff];
            *colon = &(*linebuf)[colondiff];
         }
         if (isenc != 3)
            *cp++ = ' ';
         su_mem_copy(cp, cp2, c);
         cp += c;
      }
      *cp = '\0';
      break;
   }

   mx_fs_linepool_release(line2, line2size);

   NYD2_OU;
   return rem;
}

static char const *
a_header_date_token(char const *cp){
   NYD2_IN;

   for(;;){
      if(*cp == '\0'){
         cp = NIL;
         break;
      }

      if(*cp == '('){
         uz nesting;

         nesting = 1;
         do switch(*++cp){
         case '(':
            ++nesting;
            break;
         case ')':
            --nesting;
            break;
         }while(nesting > 0 && *cp != '\0'); /* XXX error? */
      }else if(su_cs_is_blank(*cp) || *cp == ',')
         ++cp;
      else
         break;
   }

   NYD2_OU;
   return cp;
}

FL char const *
myaddrs(struct header *hp) /* TODO vanish! */
{
   /* TODO myaddrs()+myorigin() should vanish, these should simply be *from*
    * TODO and *sender*, respectively.  So either it is *from* or *sender*,
    * TODO or what we parsed as From:/Sender: from a template.  This latter
    * TODO should set *from* / *sender* in a scope, we should use *sender*:
    * TODO *sender* should be set to the real *from*! */
   boole issnd;
   struct mx_name *np;
   char const *rv, *mta;
   NYD_IN;

   if(hp != NULL && (np = hp->h_from) != NIL){
      if((rv = np->n_fullname) != NIL)
         goto jleave;
      if((rv = np->n_name) != NIL)
         goto jleave;
   }

   /* Verified once variable had been set */
   if((rv = ok_vlook(from)) != NIL)
      goto jleave;

   /* When invoking *sendmail* directly, it's its task to generate an otherwise
    * undeterminable From: address.  However, if the user sets *hostname*,
    * accept his desire (n_nodename() will use it) */
   if(ok_vlook(hostname) != NIL)
      goto jnodename;
   if(ok_vlook(smtp) != NIL || /* TODO obsolete -> mta */
         /* TODO pretty hacky for now (this entire fun), later: url_creat()! */
         ((mta = mx_url_servbyname(ok_vlook(mta), NIL, &issnd)) != NIL &&
         *mta != '\0' && issnd))
      goto jnodename;
jleave:
   NYD_OU;
   return rv;

jnodename:{
      char *cp;
      char const *hn, *ln;
      uz i;

      hn = n_nodename(TRU1);
      ln = ok_vlook(LOGNAME); /* TODO I'd like to have USER@HOST --> from */
      i = su_cs_len(ln) + su_cs_len(hn) + 1 +1;
      rv = cp = n_autorec_alloc(i);
      su_cs_pcopy(su_cs_pcopy(su_cs_pcopy(cp, ln), n_at), hn);
   }
   goto jleave;
}

FL char const *
myorigin(struct header *hp){ /* TODO vanish! see myaddrs() */
   char const *rv, *ccp;
   struct mx_name *np;
   NYD2_IN;

   rv = NIL;

   if((ccp = myaddrs(hp)) != NIL && (np = mx_name_parse(ccp, GIDENT)) != NIL){
      if(np->n_flink == NIL)
         rv = ccp;
      /* Verified upon variable set time */
      else if((ccp = ok_vlook(sender)) != NIL)
         rv = ccp;
      /* TODO why not else rv = n_poption_arg_r; ?? */
   }

   NYD2_OU;
   return rv;
}

FL boole
is_head(char const *linebuf, uz linelen, boole check_rfc4155)
{
   char date[n_FROM_DATEBUF];
   boole rv;
   NYD2_IN;

   if ((rv = (linelen >= 5 && !su_mem_cmp(linebuf, "From ", 5))) &&
         check_rfc4155 &&
         (a_header_extract_date_from_from_(linebuf, linelen, date) <= 0 ||
          !a_header_is_date(date)))
      rv = TRUM1;
   NYD2_OU;
   return rv;
}

FL char const *
mx_header_is_valid_name(char const *name, boole lead_ws,
      struct str *cramp_or_nil){
   char const *cp;
   NYD_IN;

   cp = name;

   if(lead_ws){
      while(su_cs_is_blank(*cp))
         ++cp;
      name = cp;
   }

   if(cramp_or_nil != NIL)
      cramp_or_nil->s = UNCONST(char*,name);

   while(fieldnamechar(*cp))
      ++cp;
   if(cp == name){
      name = NIL;
      goto jleave;
   }

   if(cramp_or_nil != NIL)
      cramp_or_nil->l = P2UZ(cp - name);

   while(su_cs_is_blank(*cp))
      ++cp;
   if(*cp != ':'){
      name = NIL;
      goto jleave;
   }

   while(su_cs_is_blank(*++cp))
      ;
   name = cp;

jleave:
   NYD_OU;
   return name;
}

FL boole
mx_header_needs_mime(struct header const *hp, char const **charset,
      struct mx_mime_probe_charset_ctx const *mpccp){
   struct mx_name *np;
   char const *bodies[2 + 2 + 1 + 1 +1], *ocs, **cpp, *cp;
   boole rv;
   NYD_IN;
   ASSERT(charset != NIL);

   /* (In case mime_probe_head_cp() not called at all) */
   mx_MIME_PROBE_HEAD_DEFAULT_RES(rv, charset, mpccp);

   /* */
#undef a_X
#define a_X(CP) if(mx_mime_probe_head_cp(&ocs, CP, mpccp) > FAL0) goto jneed

   /* C99 */{
      struct n_header_field *hfp, *chlp[3]; /* TODO . JOINED AFTER COMPOSE! */
      u32 i;

      chlp[0] = n_poption_arg_C;
      chlp[1] = n_customhdr_list;
      chlp[2] = hp->h_user_headers;

      for(i = 0; i < NELEM(chlp); ++i)
         if((hfp = chlp[i]) != NIL){
            do
               a_X(&hfp->hf_dat[hfp->hf_nl +1]);
            while((hfp = hfp->hf_next) != NIL);
         }
   }

   /*s_mem_set(bodies, 0, sizeof(bodies));*/
   cpp = &bodies[0];

   if((np = hp->h_sender) != NIL){
      *cpp++ = np->n_name;
      *cpp++ = np->n_fullname;
   }else if((cp = ok_vlook(sender)) != NIL)
      *cpp++ = cp;

   if((np = hp->h_reply_to) != NIL){
      do{
         a_X(np->n_name);
         if(np->n_name != np->n_fullname)
            a_X(np->n_fullname);
      }while((np = np->n_flink) != NIL);
   }else if((cp = ok_vlook(reply_to)) != NIL)
      *cpp++ = cp;

   if((cp = hp->h_subject) != NIL)
      *cpp++ = cp;

   if((np = hp->h_author) != NIL){
      do{
         a_X(np->n_name);
         if(np->n_name != np->n_fullname)
            a_X(np->n_fullname);
      }while((np = np->n_flink) != NIL);
   }

   if((np = hp->h_from) != NIL){
      do{
         a_X(np->n_name);
         if(np->n_name != np->n_fullname)
            a_X(np->n_fullname);
      }while((np = np->n_flink) != NIL);
   }else if((cp = myaddrs(NIL)) != NIL)
      *cpp++ = cp;

   if((np = hp->h_mft) != NIL){
      do{
         a_X(np->n_name);
         if(np->n_name != np->n_fullname)
            a_X(np->n_fullname);
      }while((np = np->n_flink) != NIL);
   }

   if((np = hp->h_to) != NIL){
      do{
         a_X(np->n_name);
         if(np->n_name != np->n_fullname)
            a_X(np->n_fullname);
      }while((np = np->n_flink) != NIL);
   }

   if((np = hp->h_cc) != NIL){
      do{
         a_X(np->n_name);
         if(np->n_name != np->n_fullname)
            a_X(np->n_fullname);
      }while((np = np->n_flink) != NIL);
   }

   if((np = hp->h_bcc) != NIL){
      do{
         a_X(np->n_name);
         if(np->n_name != np->n_fullname)
            a_X(np->n_fullname);
      }while((np = np->n_flink) != NIL);
   }

   /* */
   ASSERT(cpp < &bodies[NELEM(bodies)]);
   *cpp = NIL;
   for(cpp = &bodies[0]; (cp = *cpp++) != NIL;)
      a_X(cp);

jleave:
   NYD_OU;
   return rv;
jneed:
   *charset = ocs;
   rv = TRU1;
   goto jleave;
}

FL boole
n_header_put4compose(FILE *fp, struct header *hp){
   boole rv;
   int t;
   NYD_IN;

   t = GTO | GSUBJECT | GCC | GBCC | GBCC_IS_FCC | GFILES |
         GREF_IRT | GNL | GCOMMA;
   if((hp->h_from != NULL || myaddrs(hp) != NULL) ||
         (hp->h_sender != NULL || ok_vlook(sender) != NULL) ||
         (hp->h_reply_to != NULL || ok_vlook(reply_to) != NULL) ||
            ok_vlook(replyto) != NULL /* v15compat, OBSOLETE */ ||
         hp->h_list_post != NULL || (hp->h_flags & HF_LIST_REPLY))
      t |= GIDENT;

   rv = n_puthead(SEND_TODISP, TRUM1, fp, hp, t);

   NYD_OU;
   return rv;
}

FL void
n_header_extract(enum n_header_extract_flags hef, FILE *fp, struct header *hp,
   s8 *checkaddr_err_or_null)
{
   struct str suffix;
   struct n_header_field **hftail;
   struct header nh, *hq = &nh;
   char *linebuf, *colon;
   uz linesize, seenfields = 0;
   int c;
   boole clear_ref;
   long lc;
   off_t firstoff;
   char const *val, *cp;
   NYD_IN;

   mx_fs_linepool_aquire(&linebuf, &linesize);
   if(linebuf != NIL)
      linebuf[0] = '\0';

   su_mem_set(hq, 0, sizeof *hq);
   if(hef & n_HEADER_EXTRACT_PREFILL_RECIPIENTS){
      hq->h_to = hp->h_to;
      hq->h_cc = hp->h_cc;
      hq->h_bcc = hp->h_bcc;
   }
   hftail = &hq->h_user_headers;

   if((firstoff = ftell(fp)) == -1)
      goto jeseek;
   for (lc = 0; readline_restart(fp, &linebuf, &linesize, 0) > 0; ++lc)
      ;
   c = fseek(fp, firstoff, SEEK_SET);
   clearerr(fp);
   if(c != 0){
jeseek:
      if(checkaddr_err_or_null != NULL)
         *checkaddr_err_or_null = -1;
      n_err("I/O error while parsing headers, operation aborted\n");
      goto jleave;
   }

   /* TODO yippieia, cat(check(name_parse)) :-) */
   clear_ref = TRU1;
   while ((lc = a_gethfield(hef, fp, &linebuf, &linesize, lc, &colon)) >= 0) {
      struct mx_name *np;

      /* Explicitly allow mx_EAF_NAME for some addressees since aliases are not
       * yet expanded when we parse these! */
      if ((val = n_header_get_field(linebuf, "to", &suffix)) != NULL) {
         ++seenfields;
         if(suffix.s != NIL && suffix.l > 0 &&
               !su_cs_starts_with_case_n("single", suffix.s, suffix.l))
            goto jebadhead;
         np = (suffix.s != NIL
               ? mx_name_parse_as_one : mx_name_parse)(val, GTO);
         if(np != NIL)
            np = mx_namelist_check(np,
                  (mx_EACM_NORMAL | mx_EAF_NAME | mx_EAF_MAYKEEP),
                  checkaddr_err_or_null);
         if(np != NIL)
            hq->h_to = cat(hq->h_to, np);
      } else if ((val = n_header_get_field(linebuf, "cc", &suffix)) != NULL) {
         ++seenfields;
         if(suffix.s != NIL && suffix.l > 0 &&
               !su_cs_starts_with_case_n("single", suffix.s, suffix.l))
            goto jebadhead;
         hq->h_cc = cat(hq->h_cc, mx_namelist_check(
               ((suffix.s != NIL
                  ? mx_name_parse_as_one : mx_name_parse)(val, GCC)),
               (mx_EACM_NORMAL | mx_EAF_NAME | mx_EAF_MAYKEEP),
               checkaddr_err_or_null));
      } else if ((val = n_header_get_field(linebuf, "bcc", &suffix)) != NULL) {
         ++seenfields;
         if(suffix.s != NIL && suffix.l > 0 &&
               !su_cs_starts_with_case_n("single", suffix.s, suffix.l))
            goto jebadhead;
         hq->h_bcc = cat(hq->h_bcc, mx_namelist_check(
               ((suffix.s != NIL
                  ? mx_name_parse_as_one : mx_name_parse)(val, GBCC)),
               (mx_EACM_NORMAL | mx_EAF_NAME | mx_EAF_MAYKEEP),
               checkaddr_err_or_null));
      } else if ((val = n_header_get_field(linebuf, "fcc", NULL)) != NULL) {
         if(hef & n_HEADER_EXTRACT__MODE_MASK){
            ++seenfields;
            hq->h_fcc = cat(hq->h_fcc, mx_name_parse_fcc(val));
         }else
            goto jebadhead;
      }else if((val = n_header_get_field(linebuf, "author", NIL)) != NIL){
         if(hef & n_HEADER_EXTRACT_FULL){
            ++seenfields;
            hq->h_author = cat(hq->h_author,
                  mx_namelist_check(mx_name_parse(val, GIDENT | GFULLEXTRA),
                     mx_EACM_STRICT, NIL));
         }
      } else if ((val = n_header_get_field(linebuf, "from", NULL)) != NULL) {
         if(hef & n_HEADER_EXTRACT_FULL){
            ++seenfields;
            hq->h_from = cat(hq->h_from,
                  mx_namelist_check(mx_name_parse(val, GIDENT | GFULLEXTRA),
                     mx_EACM_STRICT, NIL));
         }
      }else if((val = n_header_get_field(linebuf, "reply-to", NIL)) != NIL){
         ++seenfields;
         hq->h_reply_to = cat(hq->h_reply_to,
               mx_namelist_check(mx_name_parse(val, GIDENT),
                  mx_EACM_STRICT | mx_EACM_NONAME, NIL));
      }else if((val = n_header_get_field(linebuf, "sender", NULL)) != NULL){
         if(hef & n_HEADER_EXTRACT_FULL){
            ++seenfields;
            hq->h_sender = mx_namelist_check(mx_name_parse_as_one(val,
                  GIDENT | GFULLEXTRA), mx_EACM_STRICT, NIL);
         } else
            goto jebadhead;
      }else if((val = n_header_get_field(linebuf, "subject", NULL)) != NULL){
         ++seenfields;
         for(cp = val; su_cs_is_blank(*cp); ++cp)
            ;
         cp = hq->h_subject = (hq->h_subject != NIL)
               ? savecatsep(hq->h_subject, ' ', cp) : savestr(cp);

         /* But after conscious user edit perform a subject cleanup */
         if(!(hp->h_flags & HF_USER_EDITED)){
            BITENUM(u32,mx_header_subject_edit_flags) hsef;

            switch(hp->h_flags & HF_CMD_MASK){
            default:
               hsef = HF_NONE;
               break;
            case HF_CMD_forward:
               hsef = mx_HEADER_SUBJECT_EDIT_TRIM_FWD |
                     mx_HEADER_SUBJECT_EDIT_PREPEND_FWD;
               break;
            case HF_CMD_Lreply:
            case HF_CMD_Reply:
            case HF_CMD_reply:
               hsef = mx_HEADER_SUBJECT_EDIT_TRIM_RE |
                     mx_HEADER_SUBJECT_EDIT_PREPEND_RE;
               break;
            }

            if(hsef != HF_NONE)
               hp->h_subject = mx_header_subject_edit(cp, hsef);
         }
      }
      /* The remaining are mostly hacked in and thus TODO -- at least in
       * TODO respect to their content checking */
      else if((val = n_header_get_field(linebuf, "message-id", NIL)) != NIL){
         if(hef & n_HEADER_EXTRACT__MODE_MASK){
            np = mx_name_parse(val, GREF);
            if(np == NIL || np->n_flink != NIL)
               goto jebadhead;
            np = mx_namelist_check(np,
                  (/*mx_EACM_STRICT | TODO '/' valid!! */ mx_EACM_NOLOG |
                   mx_EACM_NONAME), NIL);
            if(np == NIL)
               goto jebadhead;
            ++seenfields;
            hq->h_message_id = np;
         }else
            goto jebadhead;
      }else if((val = n_header_get_field(linebuf, "in-reply-to", NIL)) != NIL){
         if(hef & n_HEADER_EXTRACT__MODE_MASK){
            np = mx_name_parse(val, GREF);
            if(np != NIL && np->n_flink != NIL)
               np = NIL;
            if(np != NIL)
               np = mx_namelist_check(np,
                     (/*mx_EACM_STRICT| TODO '/' valid!*/ mx_EACM_NOLOG |
                      mx_EACM_NONAME), NIL);
            if(np != NIL)
               ++seenfields;
            hq->h_in_reply_to = np;

            /* Break thread if In-Reply-To: has been modified */
            if((clear_ref = (np == NIL)) || (hp->h_in_reply_to != NIL &&
                  su_cs_cmp_case(hp->h_in_reply_to->n_name, np->n_name))){
               clear_ref = TRU1;

               /* Create thread of only replied-to message if it is - */
               if(np != NIL && !su_cs_cmp(np->n_name, n_hy)){
                  clear_ref = TRUM1;
                  hq->h_in_reply_to = hp->h_in_reply_to;
               }
            }
         }else
            goto jebadhead;
      }else if((val = n_header_get_field(linebuf, "references", NULL)
            ) != NULL){
         if(hef & n_HEADER_EXTRACT__MODE_MASK){
            ++seenfields;
            /* TODO Limit number of references TODO better on parser side */
            hq->h_ref = cat(hq->h_ref,
                  mx_namelist_check(mx_name_parse(val, GREF),
                     (/*mx_EACM_STRICT | TODO '/' valid!! */ mx_EACM_NOLOG |
                     mx_EACM_NONAME), NIL));
         }else
            goto jebadhead;
      }
      /* and that is very hairy */
      else if((val = n_header_get_field(linebuf, "mail-followup-to", NULL)
               ) != NULL){
         if(hef & n_HEADER_EXTRACT__MODE_MASK){
            ++seenfields;
            hq->h_mft = cat(hq->h_mft,
                  mx_namelist_check(mx_name_parse(val, GIDENT),
                     (/*mx_EACM_STRICT | TODO '/' valid! | mx_EACM_NOLOG | */
                     mx_EACM_NONAME), checkaddr_err_or_null));
         }else
            goto jebadhead;
      }else if((hef & n_HEADER_EXTRACT_COMPOSE_MODE) &&
            (val = a_header_extract_ignore_field_XXX(linebuf)) != NIL)
         n_err(_("Ignoring header field: %s\n"), val);
      /* A free-form header; get_field() did some verification already */
      else{
         struct str hfield;
         struct n_header_field *hfp;
         uz bl;

         if((cp = mx_header_is_valid_name(linebuf, FAL0, &hfield)) == NIL){
jebadhead:
            n_err(_("Ignoring header field: %s\n"), linebuf);
            continue;
         }
         bl = su_cs_len(cp) +1;

         ++seenfields;
         *hftail =
         hfp = su_AUTO_ALLOC(VSTRUCT_SIZEOF(struct n_header_field,hf_dat) +
               hfield.l +1 + bl);
            hftail = &hfp->hf_next;
         hfp->hf_next = NIL;
         hfp->hf_nl = S(u32,hfield.l);
         hfp->hf_bl = S(u32,bl - 1);
         su_mem_copy(hfp->hf_dat, hfield.s, hfield.l);
            hfp->hf_dat[hfield.l++] = '\0';
            su_mem_copy(&hfp->hf_dat[hfield.l], cp, bl);
      }
   }

   /* In case the blank line after the header has been edited out.  Otherwise,
    * fetch the header separator */
   if (linebuf != NULL) {
      if (linebuf[0] != '\0') {
         for (cp = linebuf; *(++cp) != '\0';)
            ;
         fseek(fp, (long)-P2UZ(1 + cp - linebuf), SEEK_CUR);
      } else {
         if ((c = getc(fp)) != '\n' && c != EOF)
            ungetc(c, fp);
      }
   }

   if (seenfields > 0 &&
         (checkaddr_err_or_null == NULL || *checkaddr_err_or_null == 0)) {
      hp->h_to = hq->h_to;
      hp->h_cc = hq->h_cc;
      hp->h_bcc = hq->h_bcc;
      hp->h_author = hq->h_author;
      hp->h_from = hq->h_from;
      hp->h_reply_to = hq->h_reply_to;
      hp->h_sender = hq->h_sender;
      if(hq->h_subject != NULL ||
            (hef & n_HEADER_EXTRACT__MODE_MASK) != n_HEADER_EXTRACT_FULL)
         hp->h_subject = hq->h_subject;
      hp->h_user_headers = hq->h_user_headers;

      if(hef & n_HEADER_EXTRACT__MODE_MASK){
         hp->h_fcc = hq->h_fcc;
         if(clear_ref)
            hp->h_ref = (clear_ref != TRUM1) ? NIL : hq->h_in_reply_to;
         else if(hef & n_HEADER_EXTRACT_FULL)
            hp->h_ref = hq->h_ref;
         hp->h_message_id = hq->h_message_id;
         hp->h_in_reply_to = hq->h_in_reply_to;
         /* TODO For now the user cannot force "throwing away of M-F-T:" by
          * TODO simply deleting the header; it is possible to adjust the
          * TODO content, but is still mangled further on: very bad */
         if(hq->h_mft != NULL)
            hp->h_mft = hq->h_mft;

         /* And perform additional validity checks so that we don't bail later
          * on TODO this is good and the place where this should occur,
          * TODO unfortunately a lot of other places do again and blabla */
         if(hp->h_from == NULL)
            hp->h_from = n_poption_arg_r;
         else if((hef & n_HEADER_EXTRACT_FULL) &&
               hp->h_from->n_flink != NULL && hp->h_sender == NULL)
            hp->h_sender = mx_name_parse_as_one(ok_vlook(sender),
                  GIDENT | GFULLEXTRA);
      }
   }else if(hef & n_HEADER_EXTRACT_COMPOSE_MODE)
      n_err(_("Message header remains unchanged by template\n"));

jleave:
   mx_fs_linepool_release(linebuf, linesize);
   NYD_OU;
}

FL char *
hfield_mult(char const *field, struct message *mp, int mult
      su_DVL_LOC_ARGS_DECL)
{
   FILE *ibuf;
   struct str hfs;
   long lc;
   uz linesize;
   char *linebuf, *colon;
   char const *hfield;
   NYD_IN;

   mx_fs_linepool_aquire(&linebuf, &linesize);

   /* There are (spam) messages which have header bytes which are many KB when
    * joined, so resize a single heap storage until we are done if we shall
    * collect a field that may have multiple bodies; only otherwise use the
    * string dope directly */
   su_mem_set(&hfs, 0, sizeof hfs);

   if ((ibuf = setinput(&mb, mp, NEED_HEADER)) == NULL)
      goto jleave;
   if ((lc = mp->m_lines - 1) < 0)
      goto jleave;

   if ((mp->m_flag & MNOFROM) == 0 &&
         (readline_restart)(ibuf, &linebuf, &linesize, 0  su_DVL_LOC_ARGS_USE
            ) < 0)
      goto jleave;
   while (lc > 0) {
      if ((lc = a_gethfield(n_HEADER_EXTRACT_NONE, ibuf, &linebuf, &linesize,
            lc, &colon)) < 0)
         break;
      if ((hfield = n_header_get_field(linebuf, field, NULL)) != NULL &&
            *hfield != '\0') {
         if (mult)
            n_str_add_buf(&hfs, hfield, su_cs_len(hfield));
         else {
            hfs.s = savestr(hfield);
            break;
         }
      }
   }

jleave:
   mx_fs_linepool_release(linebuf, linesize);
   if (mult && hfs.s != NULL) {
      colon = savestrbuf(hfs.s, hfs.l);
      n_free(hfs.s);
      hfs.s = colon;
   }
   NYD_OU;
   return hfs.s;
}

FL char const *
n_header_get_field(char const *linebuf, char const *field,
   struct str *suffix_or_nil)
{
   char const *rv = NULL;
   NYD2_IN;

   if(suffix_or_nil != su_NIL)
      suffix_or_nil->s = su_NIL;

   while (su_cs_to_lower(*linebuf) == su_cs_to_lower(*field)) {
      ++linebuf;
      ++field;
   }

   if(*field != '\0')
      goto jleave;

   if(suffix_or_nil != su_NIL && *linebuf == '?'){
      char c;
      uz i;

      for(i = 0; (c = *linebuf) != '\0'; ++linebuf, ++i)
         if(su_cs_is_blank(c) || c == ':')
            break;
      if(i > 0){
         suffix_or_nil->l = --i;
         suffix_or_nil->s = UNCONST(char*,&linebuf[-i]);
      }
   }

   while(su_cs_is_blank(*linebuf))
      ++linebuf;
   if(*linebuf++ != ':')
      goto jleave;
   while(su_cs_is_blank(*linebuf)) /* TODO header parser.. strip trailing WS */
      ++linebuf;

   rv = linebuf;
jleave:
   NYD2_OU;
   return rv;
}

FL struct mx_name *
mx_header_list_post_of(struct message *mp){ /* FIXME NOW A SUPER HACK */
   char *cp;
   struct mx_name *rv;
   NYD_IN;

   rv = NIL;

   /* RFC 2369 says this is a potential list, in preference order */
   if((cp = hfield1("list-post", mp)) != NIL &&
         (rv = mx_name_parse(cp, GSPECIAL | GTRASH_HACK)) != NIL){
      do{
         if(!su_cs_cmp_case(rv->n_name, "no")){
            rv = R(struct mx_name*,-1);
            break;
         }else if((cp = mx_url_mailto_to_address(rv->n_name)) != NIL){
            rv = mx_name_parse_as_one(cp, GSPECIAL | GTRASH_HACK);
            if(rv != NIL && !mx_name_is_invalid(rv, (mx_EACM_STRICT |
                     ((n_poption & n_PO_D_V) ? 0 : mx_EACM_NOLOG))))
               break;
         }
      }while((rv = rv->n_flink) != NIL);
   }

   NYD_OU;
   return rv;
}

FL struct mx_name *
mx_header_sender_of(struct message *mp){
   struct mx_name *rv;
   char const *cp;
   NYD2_IN;

   if((cp = hfield1("from", mp)) != NIL && *cp != '\0' &&
         (rv = mx_name_parse(cp, GIDENT)) != NIL && rv->n_flink == NIL){
   }else if((cp = hfield1("sender", mp)) != NIL && *cp != '\0' &&
         (rv = mx_name_parse(cp, GIDENT)) != NIL){
   }else
      rv = NIL;

   NYD2_OU;
   return rv;
}

FL char *
n_header_senderfield_of(struct message *mp){
   char *cp;
   struct mx_name *np;
   NYD_IN;

   if((np = mx_header_sender_of(mp)) != NIL){
      cp = np->n_fullname;
      goto jleave;
   }

   /* C99 */{
      char *linebuf, *namebuf, *cp2;
      uz linesize, namesize;
      FILE *ibuf;
      int f1st = 1;

      mx_fs_linepool_aquire(&linebuf, &linesize);
      mx_fs_linepool_aquire(&namebuf, &namesize);

      /* TODO And fallback only works for MBOX -> VFS!! then: test!
       * (TODO as of today tested by accident by t_attachments[5,6] */
      namebuf = su_REALLOC(namebuf, namesize = mx_LINESIZE);
      namebuf[0] = 0;
      if (mp->m_flag & MNOFROM)
         goto jout;
      if ((ibuf = setinput(&mb, mp, NEED_HEADER)) == NULL)
         goto jout;
      if (readline_restart(ibuf, &linebuf, &linesize, 0) < 0)
         goto jout;

jnewname:
      if (namesize <= linesize)
         namebuf = n_realloc(namebuf, namesize = linesize +1);
      for (cp = linebuf; *cp != '\0' && *cp != ' '; ++cp)
         ;
      for (; su_cs_is_blank(*cp); ++cp)
         ;
      for (cp2 = namebuf + su_cs_len(namebuf);
            *cp && !su_cs_is_blank(*cp) &&
            PCMP(cp2, <, namebuf + namesize -1);)
         *cp2++ = *cp++;
      *cp2 = '\0';

      if (readline_restart(ibuf, &linebuf, &linesize, 0) < 0)
         goto jout;
      if ((cp = su_cs_find_c(linebuf, 'F')) == NULL)
         goto jout;
      if (su_cs_cmp_n(cp, "From", 4))
         goto jout;
      if (namesize <= linesize)
         namebuf = n_realloc(namebuf, namesize = linesize + 1);

      /* UUCP from 976 (we do not support anyway!) TODO NEVER TESTED! */
      while ((cp = su_cs_find_c(cp, 'r')) != NULL) {
         if (su_cs_cmp_n(cp, "remote", 6) == 0) {
            if ((cp = su_cs_find_c(cp, 'f')) == NIL)
               break;
            if (su_cs_cmp_n(cp, "from", 4) != 0)
               break;
            cp += 4;
            if(*cp != ' ')
               break;
            cp++;
            if (f1st) {
               su_cs_copy_n(namebuf, cp, namesize);
               f1st = 0;
            } else {
               cp2 = su_cs_rfind_c(namebuf, '!') + 1;
               su_cs_copy_n(cp2, cp, P2UZ(namebuf + namesize - cp2));
            }
            namebuf[namesize - 2] = '!';
            namebuf[namesize - 1] = '\0';
            goto jnewname;
         }
         cp++;
      }
jout:
      if (*namebuf != '\0' || ((cp = hfield1("return-path", mp))) == NULL ||
            *cp == '\0')
         cp = savestr(namebuf);

      mx_fs_linepool_release(namebuf, namesize);
      mx_fs_linepool_release(linebuf, linesize);
   }

jleave:
   NYD_OU;
   return cp;
}

FL char *
mx_header_subject_edit(char const *subp,
      BITENUM(u32,mx_header_subject_edit_flags) hsef){ /* {{{ */
   struct{
      u8 len;
      char dat[7];
   } const *pp, *base,
         re_ign[] = { /* Update *reply-strings* manual upon change! */
      {3, "re:"},
      {3, "aw:"}, {5, "antw:"}, /* de */
      {3, "wg:"}, /* Seen too often in the wild */
      {0, ""}
   },
         fwd_ign[] = {
      {4, "fwd:"},
      {0, ""}
   };

   struct str ltag, in, out;
   char *re_st_base, *re_st, *re_st_x;
   uz re_l;
   boole any, any_real;
   BITENUM(u32,mx_header_subject_edit_flags) hsef_any;
   void *lofi_snap;
   NYD_IN;

   lofi_snap = su_mem_bag_lofi_snap_create(su_MEM_BAG_SELF);
   hsef_any = mx_HEADER_SUBJECT_EDIT_NONE;
   any = any_real = FAL0;
   UNINIT(re_l, 0);
   re_st_base = re_st = NIL;
   ltag.l = 0;

   if(subp == NIL){
      subp = su_empty;
      hsef &= ~mx_HEADER_SUBJECT_EDIT_DECODE_MIME;
      goto jleave;
   }

   if(hsef & mx_HEADER_SUBJECT_EDIT_DECODE_MIME){
      hsef ^= mx_HEADER_SUBJECT_EDIT_DECODE_MIME;
      hsef |= mx_HEADER_SUBJECT_EDIT_DUP;

      in.l = su_cs_len(in.s = UNCONST(char*,subp));
      if(!mx_mime_display_from_header(&in, &out,
            mx_MIME_DISPLAY_ICONV | mx_MIME_DISPLAY_ISPRINT)){
         subp = su_empty;
         goto jleave;
      }

      subp = su_LOFI_ALLOC(out.l +1);
      su_mem_copy(UNCONST(char*,subp), out.s, out.l +1);
      su_FREE(out.s);
   }

   /* Skip leading WS once and for all no matter what */
   while(su_cs_is_space(*subp))
      ++subp;

   if(!(hsef & mx_HEADER_SUBJECT_EDIT_TRIM_ALL))
      goto jleave;

   if(hsef & mx_HEADER_SUBJECT_EDIT_TRIM_RE){
      hsef ^= mx_HEADER_SUBJECT_EDIT_TRIM_RE;
      hsef_any |= mx_HEADER_SUBJECT_EDIT_TRIM_RE;
      base = re_ign;

      if((re_st_x = ok_vlook(reply_strings)) != NIL &&
            (re_l = su_cs_len(re_st_x)) > 0){
         /* To avoid too much noise on buffers, just allocate a buffer twice
          * as large and use the tail as a cs_sep_c() buffer */
         re_st = re_st_base = su_LOFI_ALLOC(++re_l * 2); /* XXX overflow */
         su_mem_copy(re_st, re_st_x, re_l);
      }
   }else /*if(hsef & mx_HEADER_SUBJECT_EDIT_TRIM_FWD)*/{
      hsef ^= mx_HEADER_SUBJECT_EDIT_TRIM_FWD;
      hsef_any |= mx_HEADER_SUBJECT_EDIT_TRIM_FWD;
      base = fwd_ign;
   }

jagain:
   while(*subp != '\0'){
      while(su_cs_is_space(*subp))
         ++subp;

      /* Newer mailman (i think) edit subjects and place the tag first anyhow.
       * To be able to deal with "X: X: [TAG]" and "[TAG] X: X:" do both.
       * This may be wrong, but when the hierarchy is broken it is kaputt.
       * Restrict "tag" to non-WS and least of size 1 */
      if(*subp == '['){
         if(any_real < FAL0)
            goto jleave;
         any_real = TRUM1;

         for(ltag.s = UNCONST(char*,subp);;){
            char c;

            c = *++ltag.s;

            if(c == ']'){
               c = *++ltag.s;
               if(c == '\0')
                  break;
               ltag.l = P2UZ(ltag.s - subp);
               if(ltag.l == 2){
                  ltag.l = 0;
                  break;
               }
               ltag.s = UNCONST(char*,subp);
               subp += ltag.l;
               goto jagain;
            }else if(c == '\0' || su_cs_is_space(c))
               break;
         }
      }

      for(pp = base; pp->len > 0; ++pp)
         if(su_cs_starts_with_case(subp, pp->dat) &&
               su_cs_is_space(subp[pp->len])){
            subp += pp->len;
            any = any_real |= TRU1;
            goto jagain;
         }

      if(re_st != NIL){
         char *cp;

         su_mem_copy(re_st_x = &re_st[re_l], re_st, re_l);
         while((cp = su_cs_sep_c(&re_st_x, ',', TRU1)) != NIL)
            if(su_cs_starts_with_case(subp, cp)){
               uz i;

               i = su_cs_len(cp);
               if(su_cs_is_space(subp[i])){
                  subp += i;
                  any = any_real |= TRU1;
                  goto jagain;
               }
            }
      }

      break;
   }

   if(hsef & mx_HEADER_SUBJECT_EDIT_TRIM_FWD){
      hsef ^= mx_HEADER_SUBJECT_EDIT_TRIM_FWD;
      hsef_any |= mx_HEADER_SUBJECT_EDIT_TRIM_FWD;
      base = fwd_ign;
      re_st = NIL;
      any = FAL0;
      goto jagain;
   }

   /* May have had "Re: Fwd: Re:" with _ALL -- trimmed to "Re:"!  Restart! */
   if(any && (hsef_any & (mx_HEADER_SUBJECT_EDIT_TRIM_FWD |
            mx_HEADER_SUBJECT_EDIT_TRIM_RE)
         ) == (mx_HEADER_SUBJECT_EDIT_TRIM_FWD |
            mx_HEADER_SUBJECT_EDIT_TRIM_RE)){
      hsef |= mx_HEADER_SUBJECT_EDIT_TRIM_FWD;
      hsef_any = mx_HEADER_SUBJECT_EDIT_TRIM_RE;
      base = re_ign;
      re_st = re_st_base;
      any = FAL0;
      goto jagain;
   }

jleave:
   re_l = su_cs_len(subp);
   out.s = su_LOFI_ALLOC(ltag.l + 1 + sizeof("Fwd: ") -1 + re_l +1);
   out.l = 0;

   /* Anything trimmed, we may need to prepend, too */
   any = FAL0;
   if(re_l > 0 && (hsef & mx_HEADER_SUBJECT_EDIT__PREPEND_MASK)){
      any = TRU1;
      if(hsef & mx_HEADER_SUBJECT_EDIT_PREPEND_RE)
         in.s = UNCONST(char*,"Re: "), in.l = sizeof("Re: ") -1;
      else
         in.s = UNCONST(char*,"Fwd: "), in.l = sizeof("Fwd: ") -1;
      su_mem_copy(out.s, in.s, in.l);
      out.l = in.l;
   }else if((hsef & mx_HEADER_SUBJECT_EDIT_DUP) &&
         !(hsef & mx_HEADER_SUBJECT_EDIT_DECODE_MIME))
      any = TRU1;

   if(ltag.l > 0){
      any = TRU1;
      su_mem_copy(&out.s[out.l], ltag.s, ltag.l);
      out.l += ltag.l;
      out.s[out.l++] = ' ';
   }

   if(any || re_l == 0){
      su_mem_copy(&out.s[out.l], subp, re_l);
      out.l += re_l;
      subp = savestrbuf(out.s, out.l);
   }

   su_mem_bag_lofi_snap_gut(su_MEM_BAG_SELF, lofi_snap);

   NYD_OU;
   return UNCONST(char*,subp);
} /* }}} */

FL char const *
fakefrom(struct message *mp){
   char const * const a_h[3] = {"return-path", "from", "author"}, * const *hpp;
   char const *name;
   NYD2_IN;

   for(hpp = a_h; hpp < &a_h[NELEM(a_h)]; ++hpp){
      name = hfield1(*hpp, mp);
      if(name != NIL && *name != '\0'){
         struct mx_name *np;

         np = mx_name_parse(name, GIDENT);
         if(np != NIL && np->n_flink == NIL){
            name = np->n_name;
            goto jleave;
         }
      }
   }

   name = "MAILER-DAEMON";

jleave:
   NYD2_OU;
   return name;
}

#if defined mx_HAVE_IMAP_SEARCH || defined mx_HAVE_IMAP
FL s64
mx_header_unixtime(char const *fromline)
{
   char const *fp, *xp;
   s64 t;
   s32 i, year, month, day, hour, minute, second;
   NYD2_IN;

   for(fp = fromline; *fp != '\0' && *fp != '\n'; ++fp)
      ;
   fp -= 24;
   if(P2UZ(fp - fromline) < 7)
      goto jinvalid;

   if(fp[3] != ' ')
      goto jinvalid;

   for(i = 0;;){
      if(!su_cs_cmp_n(&fp[4], su_time_month_names_abbrev[i],
            su_TIME_MONTH_NAMES_ABBREV_LEN))
         break;
      if(!su_TIME_MONTH_IS_VALID(++i))
         goto jinvalid;
   }
   month = i + 1;

   if(fp[7] != ' ')
      goto jinvalid;

   su_idec_s32_cp(&day, &fp[8], 10, &xp);
   if (*xp != ' ' || xp != fp + 10)
      goto jinvalid;
   su_idec_s32_cp(&hour, &fp[11], 10, &xp);
   if (*xp != ':' || xp != fp + 13)
      goto jinvalid;
   su_idec_s32_cp(&minute, &fp[14], 10, &xp);
   if (*xp != ':' || xp != fp + 16)
      goto jinvalid;
   su_idec_s32_cp(&second, &fp[17], 10, &xp);
   if (*xp != ' ' || xp != fp + 19)
      goto jinvalid;
   su_idec_s32_cp(&year, &fp[20], 10, &xp);
   if (xp != fp + 24)
      goto jinvalid;

   t = su_time_gregor_to_epoch(year, month, day, hour, minute, second);
   if(t < 0)
      goto jinvalid;

   t += mx_time_tzdiff(t, NIL, NIL);

jleave:
   NYD2_OU;
   return t;

jinvalid:
   t = mx_time_now(FAL0)->ts_sec;
   goto jleave;
}
#endif /* mx_HAVE_IMAP_SEARCH || mx_HAVE_IMAP */

FL s64
mx_header_rfctime(char const *date) /* TODO su_idec_ return tests */
{
   char const *cp, *x;
   s64 t;
   s32 i, year, month, day, hour, minute, second;
   NYD2_IN;

   cp = date;

   if((cp = a_header_date_token(cp)) == NIL)
      goto jinvalid;
   if(su_cs_is_alpha(cp[0]) && su_cs_is_alpha(cp[1]) &&
         su_cs_is_alpha(cp[2]) && cp[3] == ','){
      if((cp = a_header_date_token(&cp[4])) == NIL)
         goto jinvalid;
   }
   su_idec_s32_cp(&day, cp, 10, &x);

   if((cp = a_header_date_token(x)) == NIL)
      goto jinvalid;
   for(i = 0;;){
      if(!su_cs_cmp_n(cp, su_time_month_names_abbrev[i],
            su_TIME_MONTH_NAMES_ABBREV_LEN))
         break;
      if(!su_TIME_MONTH_IS_VALID(++i))
         goto jinvalid;
   }
   month = i + 1;

   if(!su_cs_is_space(cp[su_TIME_MONTH_NAMES_ABBREV_LEN]))
      goto jinvalid;
   if((cp = a_header_date_token(&cp[su_TIME_MONTH_NAMES_ABBREV_LEN+1])) == NIL)
      goto jinvalid;
   /* RFC 5322, 4.3:
    *  Where a two or three digit year occurs in a date, the year is to be
    *  interpreted as follows: If a two digit year is encountered whose
    *  value is between 00 and 49, the year is interpreted by adding 2000,
    *  ending up with a value between 2000 and 2049.  If a two digit year
    *  is encountered with a value between 50 and 99, or any three digit
    *  year is encountered, the year is interpreted by adding 1900 */
   su_idec_s32_cp(&year, cp, 10, &x);
   i = (int)P2UZ(x - cp);
   if (i == 2 && year >= 0 && year <= 49)
      year += 2000;
   else if (i == 3 || (i == 2 && year >= 50 && year <= 99))
      year += 1900;
   if ((cp = a_header_date_token(x)) == NULL)
      goto jinvalid;
   su_idec_s32_cp(&hour, cp, 10, &x);
   if (*x != ':')
      goto jinvalid;
   cp = &x[1];
   su_idec_s32_cp(&minute, cp, 10, &x);
   if (*x == ':') {
      cp = &x[1];
      su_idec_s32_cp(&second, cp, 10, &x);
   } else
      second = 0;

   {
      s64 epsecs;

      epsecs = su_time_gregor_to_epoch(year, month, day, hour, minute, second);
      if(epsecs < 0)
         goto jinvalid;
      t = epsecs;
   }
   if ((cp = a_header_date_token(x)) != NULL) {
      char buf[3];
      int sign = 1;

      switch (*cp) {
      case '+':
         sign = -1;
         /* FALLTHRU */
      case '-':
         ++cp;
         break;
      }
      if (su_cs_is_digit(cp[0]) && su_cs_is_digit(cp[1]) &&
            su_cs_is_digit(cp[2]) && su_cs_is_digit(cp[3])) {
         s64 tadj;

         buf[2] = '\0';
         buf[0] = cp[0];
         buf[1] = cp[1];
         su_idec_s32_cp(&i, buf, 10, NULL);
         tadj = (s64)i * 3600; /* XXX */
         buf[0] = cp[2];
         buf[1] = cp[3];
         su_idec_s32_cp(&i, buf, 10, NULL);
         tadj += (s64)i * 60; /* XXX */
         if (sign < 0)
            tadj = -tadj;
         t += tadj;
      }
      /* TODO WE DO NOT YET PARSE (OBSOLETE) ZONE NAMES
       * TODO once again, Christos Zoulas and NetBSD Mail have done
       * TODO a really good job already, but using strptime(3), which
       * TODO is not portable.  Nonetheless, WE must improve, not
       * TODO at last because we simply ignore obsolete timezones!!
       * TODO See RFC 5322, 4.3! */
   }
jleave:
   NYD2_OU;
   return t;
jinvalid:
   t = 0;
   goto jleave;
}

FL void
substdate(struct message *m)
{
   /* The Date: of faked From_ lines is traditionally the date the message was
    * written to the mail file. Try to determine this using RFC message header
    * fields, or fall back to current time */
   char const *cp;
   NYD_IN;

   m->m_time = 0;
   if ((cp = hfield1("received", m)) != NULL) {
      while ((cp = a_header_date_token(cp)) != NULL && *cp != ';') {
         do
            ++cp;
         while (su_cs_is_alnum(*cp));
      }
      if (cp && *++cp)
         m->m_time = mx_header_rfctime(cp);
   }
   if (m->m_time == 0 || m->m_time > mx_time_current.tc_time) {
      if ((cp = hfield1("date", m)) != NIL)
         m->m_time = mx_header_rfctime(cp);
   }
   if (m->m_time == 0 || m->m_time > mx_time_current.tc_time)
      m->m_time = mx_time_current.tc_time;

   NYD_OU;
}

FL char *
n_header_textual_date_info(struct message *mp, char const **color_tag_or_null){
   struct tm tmlocal;
   char *rv;
   char const *fmt, *cp;
   s64 t;
   NYD_IN;
   UNUSED(color_tag_or_null);

   t = mp->m_time;
   fmt = ok_vlook(datefield);

jredo:
   if(fmt != NULL){
      u8 i;

      cp = hfield1("date", mp);/* TODO use m_date field! */
      if(cp == NULL){
         fmt = NULL;
         goto jredo;
      }

      t = mx_header_rfctime(cp);
      rv = mx_time_ctime(t, NIL);
      cp = ok_vlook(datefield_markout_older);
      i = (*fmt != '\0');
      if(cp != NULL)
         i |= (*cp != '\0') ? 2 | 4 : 2; /* XXX no magics */

      /* May we strftime(3)? */
      if(i & (1 | 4)){
         /* This localtime(3) should not fail since rfctime(3).. but .. */
         struct tm *tmp;
         time_t t2;

         /* TODO the datetime stuff is horror: mails should be parsed into
          * TODO an object tree, and date: etc. have a datetime object, which
          * TODO verifies upon parse time; then ALL occurrences of datetime are
          * TODO valid all through the program; and: to_wire, to_user! */
         t2 = S(time_t,t);
jredo_localtime:
         if((tmp = localtime(&t2)) == NULL){
            t2 = 0;
            goto jredo_localtime;
         }
         su_mem_copy(&tmlocal, tmp, sizeof *tmp);
      }

      if((i & 2) &&
            (UCMP(64, t, >, mx_time_current.tc_time + su_TIME_DAY_SECS) ||
#define _6M ((su_TIME_YEAR_DAYS / 2) * su_TIME_DAY_SECS)
             UCMP(64, t + _6M, <, mx_time_current.tc_time))){
#undef _6M
         if((fmt = (i & 4) ? cp : NULL) == NULL){
            char *x;
            LCTA(n_FROM_DATEBUF >= 4 + 7 + 1 + 4, "buffer too small");

            x = n_autorec_alloc(n_FROM_DATEBUF);
            su_mem_set(x, ' ', 4 + 7 + 1 + 4);
            su_mem_copy(&x[4], &rv[4], 7);
            x[4 + 7] = ' ';
            su_mem_copy(&x[4 + 7 + 1], &rv[20], 4);
            x[4 + 7 + 1 + 4] = '\0';
            rv = x;
         }
#ifdef mx_HAVE_COLOUR
         if(color_tag_or_null != NULL)
            *color_tag_or_null = mx_COLOUR_TAG_SUM_OLDER;
#endif
      }else if((i & 1) == 0)
         fmt = NULL;

      if(fmt != NULL){
         uz j;

         for(j = n_FROM_DATEBUF;; j <<= 1){
            i = strftime(rv = n_autorec_alloc(j), j, fmt, &tmlocal);
            if(i != 0)
               break;
            if(j > 128){
               n_err(_("Ignoring this date format: %s\n"),
                  n_shexp_quote_cp(fmt, FAL0));
               su_cs_pcopy_n(rv, mx_time_ctime(t, NIL), j);
            }
         }
      }
   }else if(t == 0 && !(mp->m_flag & MNOFROM)){
      /* TODO eliminate this path, query the FROM_ date in setptr(),
       * TODO all other codepaths do so by themselves ALREADY ?????
       * TODO ASSERT(mp->m_time != 0);, then
       * TODO ALSO changes behaviour of datefield_markout_older */
      a_header_parse_from_(mp, rv = n_autorec_alloc(n_FROM_DATEBUF));
   }else
      rv = savestr(mx_time_ctime(t, NIL));
   NYD_OU;
   return rv;
}

FL struct mx_name *
n_header_textual_sender_info(struct message *mp, struct header *hp_or_nil,
      char **cumulation_or_null, char **addr_or_null, char **name_real_or_null,
      char **name_full_or_null, boole *is_to_or_null){
   struct n_string s_b1, s_b2, *sp1, *sp2;
   char *cp;
   struct mx_name *np, *np2;
   boole isto, b;
   NYD_IN;

   isto = FAL0;

   if((hp_or_nil != NIL && (np = hp_or_nil->h_mailx_eded_sender) != NIL) ||
         (np = mx_name_parse(n_header_senderfield_of(mp), GIDENT)) != NIL){
      if(is_to_or_null != NULL && ok_blook(showto) &&
            np->n_flink == NULL && mx_name_is_metoo_cp(np->n_name, TRU1)){
         if((cp = hfield1("to", mp)) != NIL &&
               (np2 = mx_name_parse(cp, GTO)) != NIL){
            np = np2;
            isto = TRU1;
         }
      }

      if(((b = ok_blook(showname)) && cumulation_or_null != NULL) ||
            name_real_or_null != NULL || name_full_or_null != NULL){
         uz i;

         for(i = 0, np2 = np; np2 != NULL; np2 = np2->n_flink)
            i += su_cs_len(np2->n_fullname) +3;

         sp1 = n_string_book(n_string_creat_auto(&s_b1), i);
         sp2 = (name_full_or_null == NULL) ? NULL
               : n_string_book(n_string_creat_auto(&s_b2), i);

         for(np2 = np; np2 != NULL; np2 = np2->n_flink){
            if(sp1->s_len > 0){
               sp1 = n_string_push_c(sp1, ',');
               sp1 = n_string_push_c(sp1, ' ');
               if(sp2 != NULL){
                  sp2 = n_string_push_c(sp2, ',');
                  sp2 = n_string_push_c(sp2, ' ');
               }
            }

            if((cp = mx_name_real_cp(np2->n_fullname)) == NULL || *cp == '\0')
               cp = np2->n_name;
            sp1 = n_string_push_cp(sp1, cp);
            if(sp2 != NULL)
               sp2 = n_string_push_cp(sp2, (*np2->n_fullname == '\0'
                     ? np2->n_name : np2->n_fullname));
         }

         n_string_cp(sp1);
         if(b && cumulation_or_null != NULL)
            *cumulation_or_null = sp1->s_dat;
         if(name_real_or_null != NULL)
            *name_real_or_null = sp1->s_dat;
         if(name_full_or_null != NULL)
            *name_full_or_null = n_string_cp(sp2);

         /* n_string_gut(n_string_drop_ownership(sp2)); */
         /* n_string_gut(n_string_drop_ownership(sp1)); */
      }

      if((b = (!b && cumulation_or_null != NULL)) || addr_or_null != NULL){
         cp = mx_namelist_detract(np, GCOMMA | GNAMEONLY);
         if(b)
            *cumulation_or_null = cp;
         if(addr_or_null != NULL)
            *addr_or_null = cp;
      }
   }else if(cumulation_or_null != NULL || addr_or_null != NULL ||
         name_real_or_null != NULL || name_full_or_null != NULL){
      cp = savestr(n_empty);

      if(cumulation_or_null != NULL)
         *cumulation_or_null = cp;
      if(addr_or_null != NULL)
         *addr_or_null = cp;
      if(name_real_or_null != NULL)
         *name_real_or_null = cp;
      if(name_full_or_null != NULL)
         *name_full_or_null = cp;
   }

   if(is_to_or_null != NIL)
      *is_to_or_null = isto;

   NYD_OU;
   return np;
}

FL void
setup_from_and_sender(struct header *hp){
   char const *addr;
   struct mx_name *np;
   NYD_IN;

   /* If -t parsed or composed From: then take it.  With -t we otherwise
    * want -r to be honoured in favour of *from* in order to have
    * a behaviour that is compatible with what users would expect from e.g.
    * postfix(1) */
   if((np = hp->h_from) != NIL ||
         ((n_poption & n_PO_t_FLAG) && (np = n_poption_arg_r) != NIL)){
      ;
   }else if((addr = myaddrs(hp)) != NIL)
      np = mx_name_parse(addr, GIDENT | GFULLEXTRA);

   hp->h_from = np;

   /* RFC 5322 says
    *  If the originator of the message can be indicated by a single mailbox
    *  and the author and transmitter are identical, the "Sender:" field SHOULD
    *  NOT be used.  Otherwise, both fields SHOULD appear. */
   if((np = hp->h_sender) != NIL){
   }else if((addr = ok_vlook(sender)) != NIL)
      np = mx_name_parse_as_one(addr, GIDENT | GFULLEXTRA);

   if(np != NIL && hp->h_from != NIL && hp->h_from->n_flink == NIL &&
         mx_name_is_same_address(hp->h_from, np))
      np = NIL;

   hp->h_sender = np;

   NYD_OU;
}

FL struct mx_name const *
check_from_and_sender(struct mx_name const *fromfield,
   struct mx_name const *senderfield)
{
   struct mx_name const *rv = NULL;
   NYD_IN;

   if (senderfield != NULL) {
      if (senderfield->n_flink != NULL) {
         n_err(_("The Sender: field may contain only one address\n"));
         goto jleave;
      }
      rv = senderfield;
   }

   if (fromfield != NULL) {
      if (fromfield->n_flink != NULL && senderfield == NULL) {
         n_err(_("A Sender: is required when there are multiple "
            "addresses in From:\n"));
         goto jleave;
      }
      if (rv == NULL)
         rv = fromfield;
   }

   if (rv == NULL)
      rv = (struct mx_name*)0x1;
jleave:
   NYD_OU;
   return rv;
}

FL void
mx_header_setup_references(struct header *hp, struct message *mp){
   uz i;
   uz oldmsgidlen, reflen, oldreflen;
   char *oldmsgid, *oldref, *newref;
   struct mx_name *np;
   NYD_IN;

   np = NIL;

   if((oldmsgid = hfield1("message-id", mp)) == NIL || *oldmsgid == '\0'){
      hp->h_ref = NIL;
      goto jleave;
   }
   oldmsgidlen = su_cs_len(oldmsgid);

   if((oldref = hfield1("references", mp)) != NIL){
      oldreflen = su_cs_len(oldref);
   }else
      oldreflen = 0;

   reflen = oldreflen + 2 + oldmsgidlen +1;
   newref = su_LOFI_ALLOC(reflen);

   if(oldref != NIL){
      su_mem_copy(newref, oldref, oldreflen +1);
      newref[oldreflen++] = ',';
      newref[oldreflen++] = ' ';
   }
   su_mem_copy(&newref[oldreflen], oldmsgid, oldmsgidlen +1);

   np = extract(newref, GREF);

   su_LOFI_FREE(newref);

   /* Limit number of references TODO better on parser side */
   while(np->n_flink != NIL)
      np = np->n_flink;
   for(i = 1; i <= REFERENCES_MAX; ++i){
      if(np->n_blink != NIL)
         np = np->n_blink;
      else
         break;
   }
   np->n_blink = NIL;

jleave:
   hp->h_ref = np;
   NYD_OU;
}

FL struct mx_name *
mx_header_setup_in_reply_to(struct header *hp){
   struct mx_name *np;
   NYD2_IN;

   np = NIL;

   if(hp != NIL){
      if((np = hp->h_in_reply_to) == NIL && (np = hp->h_ref) != NIL)
         while(np->n_flink != NIL)
            np = np->n_flink;

      hp->h_in_reply_to = np;
   }

   NYD2_OU;
   return np;
}

FL void
mx_header_setup_pseudo_orig(struct header *hp, struct message *mp){
   NYD2_IN;

   hp->h_mailx_orig_sender = mx_name_parse(n_header_senderfield_of(mp), GIDENT);
   hp->h_mailx_orig_from = mx_name_parse(hfield1("from", mp), GIDENT);
   hp->h_mailx_orig_to = mx_name_parse(hfield1("to", mp), GTO);
   hp->h_mailx_orig_cc = mx_name_parse(hfield1("cc", mp), GCC);
   hp->h_mailx_orig_bcc = mx_name_parse(hfield1("bcc", mp), GBCC);

   hp->h_mailx_eded_sender = ndup(hp->h_mailx_orig_sender, GIDENT);
   hp->h_mailx_eded_origin = ndup(hp->h_mailx_orig_from, GIDENT);

   NYD2_OU;
}

FL struct mx_name *
mx_header_get_reply_to(struct message *mp, struct header *hp_or_nil,
      boole append){
   struct n_string promptb, *p;
   char const *cp;
   struct mx_name *rt, *np;
   NYD_IN;

   rt = NIL;

   if((cp = ok_vlook(reply_to_honour)) == NIL)
      goto jleave;

   rt = checkaddrs(mx_name_parse(hfield1("reply-to", mp), GIDENT),
         EACM_STRICT | EACM_NONAME, NIL);
   if(rt == NIL)
      goto jleave;

   if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)){
      p = n_string_reserve(n_string_creat(p = &promptb), 1024 -1);

/* FIXME: if reply_to: equals from: simply use from! */
      p = n_string_push_cp(p, _("Honour Reply-To: "));
      for(np = rt; np != NIL; np = np->n_flink){
         if(np != rt)
            p = n_string_push_buf(p, ", ", 2);
         p = n_string_push_cp(p, np->n_fullname);
      }
   }else
      p = NIL;

   if(n_quadify(cp, UZ_MAX, (p != NIL ? n_string_cp(p) : NIL), TRU1
         ) <= FAL0)
      rt = NIL;

   if(p != NIL)
      n_string_gut(p);

   /* A Reply-To: "replaces" the content of From: */
   if(rt != NIL && hp_or_nil != NIL){
      if(!append)
         hp_or_nil->h_mailx_eded_origin = NIL;
      hp_or_nil->h_mailx_eded_origin = cat(hp_or_nil->h_mailx_eded_origin,
            ndup(rt, rt->n_type));

      if(!append && rt->n_flink == NIL)
         hp_or_nil->h_mailx_eded_sender = ndup(rt, rt->n_type);
   }

jleave:
   NYD_OU;
   return rt;
}

FL struct mx_name *
mx_header_get_mail_followup_to(struct message *mp){
   struct n_string promptb, *p;
   char const *cp;
   struct mx_name *mft, *np;
   NYD_IN;

   mft = NIL;

   if((cp = ok_vlook(followup_to_honour)) == NIL)
      goto jleave;

   mft = checkaddrs(mx_name_parse(hfield1("mail-followup-to", mp), GIDENT),
         EACM_STRICT, NIL);
   if(mft == NIL)
      goto jleave;

   if((n_psonce & n_PSO_INTERACTIVE) && !(n_pstate & n_PS_ROBOT)){
      p = n_string_reserve(n_string_creat(p = &promptb), 1024 -1);

      p = n_string_push_cp(p, _("Honour Mail-Followup-To: "));
      for(np = mft; np != NIL; np = np->n_flink){
         if(np != mft)
            p = n_string_push_buf(p, ", ", 2);
         p = n_string_push_cp(p, np->n_fullname);
      }
   }else
      p = NIL;

   if(n_quadify(cp, UZ_MAX, (p != NIL ? n_string_cp(p) : NIL), TRU1
         ) <= FAL0)
      mft = NIL;

   if(p != NIL)
      n_string_gut(p);

jleave:
   NYD_OU;
   return mft;
}

FL int
grab_headers(u32/*mx_go_input_flags*/ gif, struct header *hp,
      enum gfield gflags, int subjfirst)
{
   /* TODO grab_headers: again, check counts etc. against RFC;
    * TODO (now assumes check_from_and_sender() is called afterwards ++ */
   char const *cp;
   int errs;
   int volatile comma;
   NYD_IN;

   errs = 0;
   comma = (ok_blook(bsdcompat) || ok_blook(bsdmsgs)) ? 0 : GCOMMA;

   if(gflags & GTO)
      hp->h_to = mx_namelist_grab(gif, "To: ", hp->h_to, comma, GTO, FAL0);
   if(subjfirst && (gflags & GSUBJECT))
      hp->h_subject = mx_go_input_cp(gif, "Subject: ", hp->h_subject);
   if(gflags & GCC)
      hp->h_cc = mx_namelist_grab(gif, "Cc: ", hp->h_cc, comma, GCC, FAL0);
   if(gflags & GBCC)
      hp->h_bcc = mx_namelist_grab(gif, "Bcc: ", hp->h_bcc, comma, GBCC, FAL0);

   if(gflags & GIDENT){
      if(hp->h_from == NIL)
         hp->h_from = mx_name_parse(myaddrs(hp), GIDENT | GFULLEXTRA);
      hp->h_from = mx_namelist_grab(gif, "From: ", hp->h_from, comma,
            GIDENT | GFULLEXTRA, FAL0);
      if(hp->h_reply_to == NIL && (cp = ok_vlook(reply_to)) != NIL)
         hp->h_reply_to = mx_name_parse(cp, GIDENT);
      hp->h_reply_to = mx_namelist_grab(gif, "Reply-To: ", hp->h_reply_to,
            comma, GIDENT, FAL0);
      if(hp->h_sender == NIL && (cp = ok_vlook(sender)) != NIL)
         hp->h_sender = mx_name_parse_as_one(cp, GIDENT);
      hp->h_sender = mx_namelist_grab(gif, "Sender: ", hp->h_sender, comma,
            GIDENT, TRU1);
   }

   if(!subjfirst && (gflags & GSUBJECT))
      hp->h_subject = mx_go_input_cp(gif, "Subject: ", hp->h_subject);

   NYD_OU;
   return errs;
}

FL boole
n_header_match(struct message *mp, struct mx_srch_ctx const *scp){
   struct str fiter, in, out;
   char const *field;
   long lc;
   FILE *ibuf;
   uz linesize;
   char *linebuf, *colon;
   enum {a_NONE, a_ALL, a_ITER, a_RE} match;
   boole rv;
   NYD_IN;

   rv = FAL0;
   match = a_NONE;
   mx_fs_linepool_aquire(&linebuf, &linesize);
   UNINIT(fiter.l, 0);
   UNINIT(fiter.s, NIL);

   if((ibuf = setinput(&mb, mp, NEED_HEADER)) == NULL)
      goto jleave;
   if((lc = mp->m_lines - 1) < 0)
      goto jleave;

   if((mp->m_flag & MNOFROM) == 0 &&
         readline_restart(ibuf, &linebuf, &linesize, 0) < 0)
      goto jleave;

   /* */
   if((field = scp->sc_field) != NULL){
      if(!su_cs_cmp_case(field, "header") ||
            (field[0] == '<' && field[1] == '\0'))
         match = a_ALL;
      else{
         fiter.s = n_lofi_alloc((fiter.l = su_cs_len(field)) +1);
         match = a_ITER;
      }
#ifdef mx_HAVE_REGEX
   }else if(scp->sc_fieldre != NULL){
      match = a_RE;
#endif
   }else
      match = a_ALL;

   /* Iterate over all the headers */
   while(lc > 0){
      struct mx_name *np;

      if((lc = a_gethfield(n_HEADER_EXTRACT_NONE, ibuf, &linebuf, &linesize,
            lc, &colon)) <= 0)
         break;

      /* Is this a header we are interested in? */
      if(match == a_ITER){
         char *itercp;

         su_mem_copy(itercp = fiter.s, scp->sc_field, fiter.l +1);
         while((field = su_cs_sep_c(&itercp, ',', TRU1)) != NULL){
            /* It may be an abbreviation */
            char const x[][8] = {
               "author", "from", "to", "cc", "bcc", "subject"
            };
            uz i;
            char c1;

            if(field[0] != '\0' && field[1] == '\0'){
               c1 = su_cs_to_lower(field[0]);
               for(i = 0; i < NELEM(x); ++i){
                  if(c1 == x[i][0]){
                     field = x[i];
                     break;
                  }
               }
            }

            i = P2UZ(colon - linebuf);
            if(!su_cs_cmp_case_n(field, linebuf, i) && field[i] == '\0')
               break;
            /* Author: finds From: and Sender: too */
            if(!scp->sc_field_exists && !su_cs_cmp_case(field, "author")){
               if(i == sizeof("sender") -1 &&
                     !su_cs_cmp_case_n("sender", linebuf, i))
                  break;
               if(i == sizeof("from") -1 &&
                     !su_cs_cmp_case_n("from", linebuf, i))
                  break;
            }
         }
         if(field == NIL)
            continue;
#ifdef mx_HAVE_REGEX
      }else if(match == a_RE){
         char *cp;
         uz i;

         i = P2UZ(colon - linebuf);
         cp = su_LOFI_ALLOC(i +1);
         su_mem_copy(cp, linebuf, i);
         cp[i] = '\0';
         i = su_re_eval_cp(scp->sc_fieldre, cp, su_RE_EVAL_NONE);
         su_LOFI_FREE(cp);
         if(!i)
            continue;
#endif
      }

      /* It could be a plain existence test */
      if(scp->sc_field_exists){
         rv = TRU1;
         break;
      }

      /* Need to check the body */
      while(su_cs_is_blank(*++colon))
         ;
      in.s = colon;

      /* Shall we split into address list and match as/the addresses only?
       * TODO at some later time we should ignore and log efforts to search
       * TODO a skinned address list if we know the header has none such */
      if(scp->sc_skin){
         if((np = mx_name_parse(in.s, 0)) == NIL)
            continue;
         out.s = np->n_name;
      }else{
         np = NIL;
         in.l = su_cs_len(in.s);
         if(!mx_mime_display_from_header(&in, &out, mx_MIME_DISPLAY_ICONV))
            continue;
      }

jnext_name:
#ifdef mx_HAVE_REGEX
      if(scp->sc_bodyre != NIL)
         rv = su_re_eval_cp(scp->sc_bodyre, out.s, su_RE_EVAL_NONE);
      else
#endif
         rv = (mx_substr(out.s, scp->sc_body) != NIL);

      if(np == NULL)
         n_free(out.s);
      if(rv)
         break;
      if(np != NULL && (np = np->n_flink) != NULL){
         out.s = np->n_name;
         goto jnext_name;
      }
   }

jleave:
   if(match == a_ITER)
      n_lofi_free(fiter.s);
   mx_fs_linepool_release(linebuf, linesize);
   NYD_OU;
   return rv;
}

FL char const *
n_header_is_known(char const *name, uz len){
   static char const * const names[] = {
      /* RFC 5322 header names common here */
      "Bcc", "Cc", "From",
      "In-Reply-To", "Mail-Followup-To",
      "Message-ID", "References", "Reply-To",
      "Sender", "Subject", "To",
      /* RFCd */
      "Author", /* RFC 9057 */
      /* More known, here and there */
      "Fcc",
      /* Mailx internal temporaries */
      "Mailx-Command",
      "Mailx-Edited-Origin", "Mailx-Edited-Sender",
      "Mailx-Orig-Bcc", "Mailx-Orig-Cc", "Mailx-Orig-From",
         "Mailx-Orig-Sender", "Mailx-Orig-To",
      "Mailx-Raw-Bcc", "Mailx-Raw-Cc", "Mailx-Raw-To",
      /* Rest of RFC 5322 standard headers, almost never seen here.
       * As documented for *customhdr*, allow Comments:, Keywords: */
      /*"Comments",*/ "Date",
      /*"Keywords",*/ "Received",
      "Resent-Bcc", "Resent-Cc", "Resent-Date",
         "Resent-From", "Resent-Message-ID", "Resent-Reply-To",
         "Resent-Sender", "Resent-To",
      "Return-Path",
      NIL
   };
   char const * const *rv;
   NYD_IN;

   if(len == UZ_MAX)
      len = su_cs_len(name);

   for(rv = names; *rv != NIL; ++rv)
      if(!su_cs_cmp_case_n(*rv, name, len) && (*rv)[len] == '\0')
         break;

   NYD_OU;
   return *rv;
}

FL boole
n_header_add_custom(struct n_header_field **hflp, char const *dat, boole heap){
   struct str hname;
   uz i;
   u32 bl;
   char const *cp;
   struct n_header_field *hfp;
   NYD_IN;

   hfp = NIL;

   /* For (-C) convenience, allow leading WS */
   if((cp = mx_header_is_valid_name(dat, TRU1, &hname)) == NIL){
      cp = N_("Invalid custom header (not valid \"field: body\"): %s\n");
      goto jerr;
   }

   /* Verify the custom header does not use standard/managed field name */
   if(n_header_is_known(hname.s, hname.l) != NIL){
      cp = N_("Custom headers cannot use standard header names: %s\n");
      goto jerr;
   }

   /* Skip on over to the body */
   bl = S(u32,su_cs_len(cp));
   while(bl > 0 && su_cs_is_space(cp[bl - 1]))
      --bl;
   for(i = bl; i-- != 0;)
      if(su_cs_is_cntrl(cp[i])){
         cp = N_("Invalid custom header: contains control characters: %s\n");
         goto jerr;
      }

   if(heap == TRUM1){
      hfp = R(struct n_header_field*,-1);
      goto jleave;
   }

   i = VSTRUCT_SIZEOF(struct n_header_field,hf_dat) + hname.l +1 + bl +1;
   *hflp = hfp = heap ? su_ALLOC(i) : su_AUTO_ALLOC(i);
   hfp->hf_next = NIL;
   hfp->hf_nl = hname.l;
   hfp->hf_bl = bl;
   /* C99 */{
      char *xp;

      xp = hfp->hf_dat;
      su_mem_copy(xp, hname.s, hname.l);
      xp[hname.l] = '\0';
      xp += ++hname.l;
      if(bl > 0)
         su_mem_copy(xp, cp, bl);
      xp[bl] = '\0';
   }

jleave:
   NYD_OU;
   return (hfp != NIL);

jerr:
   n_err(V_(cp), n_shexp_quote_cp(dat, FAL0));
   goto jleave;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_HEADER
/* s-it-mode */
