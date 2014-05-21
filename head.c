/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Routines for processing and detecting headlines.
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

#ifdef HAVE_IDNA
# include <idna.h>
# include <idn-free.h>
# include <stringprep.h>
#endif

struct cmatch_data {
   size_t      tlen;    /* Length of .tdata */
   char const  *tdata;  /* Template date - see _cmatch_data[] */
};

/* Template characters for cmatch_data.tdata:
 * 'A'   An upper case char
 * 'a'   A lower case char
 * ' '   A space
 * '0'   A digit
 * 'O'   An optional digit or space
 * ':'   A colon
 * '+'  Either a plus or a minus sign */
static struct cmatch_data const  _cmatch_data[] = {
   { 24, "Aaa Aaa O0 00:00:00 0000" },       /* BSD/ISO C90 ctime */
   { 28, "Aaa Aaa O0 00:00:00 AAA 0000" },   /* BSD tmz */
   { 21, "Aaa Aaa O0 00:00 0000" },          /* SysV ctime */
   { 25, "Aaa Aaa O0 00:00 AAA 0000" },      /* SysV tmz */
   /* RFC 822-alike From_ lines do not conform to RFC 4155, but seem to be used
    * in the wild (by UW-imap) */
   { 30, "Aaa Aaa O0 00:00:00 0000 +0000" },
   /* RFC 822 with zone spec; 1. military, 2. UT, 3. north america time
    * zone strings; note that 1. is strictly speaking not correct as some
    * letters are not used, and 2. is not because only "UT" is defined */
#define __reuse      "Aaa Aaa O0 00:00:00 0000 AAA"
   { 28 - 2, __reuse }, { 28 - 1, __reuse }, { 28 - 0, __reuse },
   { 0, NULL }
};
#define _DATE_MINLEN 21

/* Skip over "word" as found in From_ line */
static char const *        _from__skipword(char const *wp);

/* Match the date string against the date template (tp), return if match.
 * See _cmatch_data[] for template character description */
static int                 _cmatch(size_t len, char const *date,
                              char const *tp);

/* Check wether date is a valid 'From_' date.
 * (Rather ctime(3) generated dates, according to RFC 4155) */
static int                 _is_date(char const *date);

/* Convert the domain part of a skinned address to IDNA.
 * If an error occurs before Unicode information is available, revert the IDNA
 * error to a normal CHAR one so that the error message doesn't talk Unicode */
#ifdef HAVE_IDNA
static struct addrguts *   _idna_apply(struct addrguts *agp);
#endif

/* Classify and check a (possibly skinned) header body according to RFC
 * *addr-spec* rules; if it (is assumed to has been) skinned it may however be
 * also a file or a pipe command, so check that first, then.
 * Otherwise perform content checking and isolate the domain part (for IDNA) */
static int                 _addrspec_check(int doskin, struct addrguts *agp);

/* Return the next header field found in the given message.
 * Return >= 0 if something found, < 0 elsewise.
 * "colon" is set to point to the colon in the header.
 * Must deal with \ continuations & other such fraud */
static int                 gethfield(FILE *f, char **linebuf, size_t *linesize,
                              int rem, char **colon);

static int                 msgidnextc(char const **cp, int *status);

/* Count the occurances of c in str */
static int                 charcount(char *str, int c);

static char const *        nexttoken(char const *cp);

static char const *
_from__skipword(char const *wp)
{
   char c = 0;
   NYD_ENTER;

   if (wp != NULL) {
      while ((c = *wp++) != '\0' && !blankchar(c)) {
         if (c == '"') {
            while ((c = *wp++) != '\0' && c != '"')
               ;
            if (c != '"')
               --wp;
         }
      }
      for (; blankchar(c); c = *wp++)
         ;
   }
   NYD_LEAVE;
   return (c == 0 ? NULL : wp - 1);
}

static int
_cmatch(size_t len, char const *date, char const *tp)
{
   int ret = 0;
   NYD_ENTER;

   while (len--) {
      char c = date[len];
      switch (tp[len]) {
      case 'a':
         if (!lowerchar(c))
            goto jleave;
         break;
      case 'A':
         if (!upperchar(c))
            goto jleave;
         break;
      case ' ':
         if (c != ' ')
            goto jleave;
         break;
      case '0':
         if (!digitchar(c))
            goto jleave;
         break;
      case 'O':
         if (c != ' ' && !digitchar(c))
            goto jleave;
         break;
      case ':':
         if (c != ':')
            goto jleave;
         break;
      case '+':
         if (c != '+' && c != '-')
            goto jleave;
         break;
      }
   }
   ret = 1;
jleave:
   NYD_LEAVE;
   return ret;
}

static int
_is_date(char const *date)
{
   struct cmatch_data const *cmdp;
   size_t dl;
   int rv = 0;
   NYD_ENTER;

   if ((dl = strlen(date)) >= _DATE_MINLEN)
      for (cmdp = _cmatch_data; cmdp->tdata != NULL; ++cmdp)
         if (dl == cmdp->tlen && (rv = _cmatch(dl, date, cmdp->tdata)))
            break;
   NYD_LEAVE;
   return rv;
}

#ifdef HAVE_IDNA
static struct addrguts *
_idna_apply(struct addrguts *agp)
{
   char *idna_utf8, *idna_ascii, *cs;
   size_t sz, i;
   NYD_ENTER;

   sz = agp->ag_slen - agp->ag_sdom_start;
   assert(sz > 0);
   idna_utf8 = ac_alloc(sz +1);
   memcpy(idna_utf8, agp->ag_skinned + agp->ag_sdom_start, sz);
   idna_utf8[sz] = '\0';

   /* GNU Libidn settles on top of iconv(3) without any fallback, so let's just
    * let it perform the charset conversion, if any should be necessary */
   if (!(options & OPT_UNICODE)) {
      char const *tcs = charset_get_lc();
      idna_ascii = idna_utf8;
      idna_utf8 = stringprep_convert(idna_ascii, "UTF-8", tcs);
      i = (idna_utf8 == NULL && errno == EINVAL);
      ac_free(idna_ascii);

      if (idna_utf8 == NULL) {
         if (i)
            fprintf(stderr, tr(179, "Cannot convert from %s to %s\n"),
               tcs, "UTF-8");
         agp->ag_n_flags ^= NAME_ADDRSPEC_ERR_IDNA | NAME_ADDRSPEC_ERR_CHAR;
         goto jleave;
      }
   }

   if (idna_to_ascii_8z(idna_utf8, &idna_ascii, 0) != IDNA_SUCCESS) {
      agp->ag_n_flags ^= NAME_ADDRSPEC_ERR_IDNA | NAME_ADDRSPEC_ERR_CHAR;
      goto jleave1;
   }

   /* Replace the domain part of .ag_skinned with IDNA version */
   sz = strlen(idna_ascii);
   i = agp->ag_sdom_start;
   cs = salloc(agp->ag_slen - i + sz +1);
   memcpy(cs, agp->ag_skinned, i);
   memcpy(cs + i, idna_ascii, sz);
   i += sz;
   cs[i] = '\0';

   agp->ag_skinned = cs;
   agp->ag_slen = i;
   NAME_ADDRSPEC_ERR_SET(agp->ag_n_flags,
      NAME_NAME_SALLOC | NAME_SKINNED | NAME_IDNA, 0);

   idn_free(idna_ascii);
jleave1:
   if (options & OPT_UNICODE)
      ac_free(idna_utf8);
   else
      idn_free(idna_utf8);
jleave:
   NYD_LEAVE;
   return agp;
}
#endif

static int
_addrspec_check(int skinned, struct addrguts *agp)
{
   char *addr, *p;
   bool_t in_quote;
   ui8_t in_domain, hadat;
   union {char c; unsigned char u;} c;
#ifdef HAVE_IDNA
   ui8_t use_idna;
#endif
   NYD_ENTER;

#ifdef HAVE_IDNA
   use_idna = ok_blook(idna_disable) ? 0 : 1;
#endif
   agp->ag_n_flags |= NAME_ADDRSPEC_CHECKED;
   addr = agp->ag_skinned;

   if (agp->ag_iaddr_aend - agp->ag_iaddr_start == 0) {
      NAME_ADDRSPEC_ERR_SET(agp->ag_n_flags, NAME_ADDRSPEC_ERR_EMPTY, 0);
      goto jleave;
   }

   /* If the field is not a recipient, it cannot be a file or a pipe */
   if (!skinned)
      goto jaddr_check;

   /* Excerpt from nail.1:
    * Recipient address specifications
    * The rules are: Any name which starts with a `|' character specifies
    * a pipe,  the  command  string  following  the `|' is executed and
    * the message is sent to its standard input; any other name which
    * contains a  `@' character  is treated as a mail address; any other
    * name which starts with a `+' character specifies a folder name; any
    * other name  which  contains  a  `/' character  but  no `!'  or `%'
    * character before also specifies a folder name; what remains is
    * treated as a mail  address */
   if (*addr == '|') {
      agp->ag_n_flags |= NAME_ADDRSPEC_ISPIPE;
      goto jleave;
   }
   if (memchr(addr, '@', agp->ag_slen) == NULL) {
      if (*addr == '+')
         goto jisfile;
      for (p = addr; (c.c = *p); ++p) {
         if (c.c == '!' || c.c == '%')
            break;
         if (c.c == '/') {
jisfile:
            agp->ag_n_flags |= NAME_ADDRSPEC_ISFILE;
            goto jleave;
         }
      }
   }

jaddr_check:
   in_quote = FAL0;
   in_domain = hadat = 0;

   for (p = addr; (c.c = *p++) != '\0';) {
      if (c.c == '"') {
         in_quote = !in_quote;
      } else if (c.u < 040 || c.u >= 0177) { /* TODO no magics: !bodychar()? */
#ifdef HAVE_IDNA
         if (in_domain && use_idna > 0) {
            if (use_idna == 1)
               NAME_ADDRSPEC_ERR_SET(agp->ag_n_flags, NAME_ADDRSPEC_ERR_IDNA,
                  c.u);
            use_idna = 2;
         } else
#endif
            break;
      } else if (in_domain == 2) {
         if ((c.c == ']' && *p != '\0') || c.c == '\\' || whitechar(c.c))
            break;
      } else if (in_quote && in_domain == 0) {
         /*EMPTY*/;
      } else if (c.c == '\\' && *p != '\0') {
         ++p;
      } else if (c.c == '@') {
         if (hadat++ > 0) {
            NAME_ADDRSPEC_ERR_SET(agp->ag_n_flags, NAME_ADDRSPEC_ERR_ATSEQ,
               c.u);
            goto jleave;
         }
         agp->ag_sdom_start = PTR2SIZE(p - addr);
         in_domain = (*p == '[') ? 2 : 1;
         continue;
      } else if (c.c == '(' || c.c == ')' || c.c == '<' || c.c == '>' ||
            c.c == ',' || c.c == ';' || c.c == ':' || c.c == '\\' ||
            c.c == '[' || c.c == ']')
         break;
      hadat = 0;
   }

   if (c.c != '\0') {
      NAME_ADDRSPEC_ERR_SET(agp->ag_n_flags, NAME_ADDRSPEC_ERR_CHAR, c.u);
      goto jleave;
   }
#ifdef HAVE_IDNA
   if (use_idna == 2)
      agp = _idna_apply(agp);
#endif
jleave:
   NYD_LEAVE;
   return ((agp->ag_n_flags & NAME_ADDRSPEC_INVALID) != 0);
}

static int
gethfield(FILE *f, char **linebuf, size_t *linesize, int rem, char **colon)
{
   char *line2 = NULL, *cp, *cp2;
   size_t line2size = 0;
   int c, isenc;
   NYD_ENTER;

   if (*linebuf == NULL)
      *linebuf = srealloc(*linebuf, *linesize = 1);
   **linebuf = '\0';
   for (;;) {
      if (--rem < 0) {
         rem = -1;
         break;
      }
      if ((c = readline_restart(f, linebuf, linesize, 0)) <= 0) {
         rem = -1;
         break;
      }
      for (cp = *linebuf; fieldnamechar(*cp); ++cp)
         ;
      if (cp > *linebuf)
         while (blankchar(*cp))
            ++cp;
      if (*cp != ':' || cp == *linebuf)
         continue;

      /* I guess we got a headline.  Handle wraparound */
      *colon = cp;
      cp = *linebuf + c;
      for (;;) {
         isenc = 0;
         while (PTRCMP(--cp, >=, *linebuf) && blankchar(*cp))
            ;
         cp++;
         if (rem <= 0)
            break;
         if (PTRCMP(cp - 8, >=, *linebuf) && cp[-1] == '=' && cp[-2] == '?')
            isenc |= 1;
         ungetc(c = getc(f), f);
         if (!blankchar(c))
            break;
         c = readline_restart(f, &line2, &line2size, 0);
         if (c < 0)
            break;
         --rem;
         for (cp2 = line2; blankchar(*cp2); ++cp2)
            ;
         c -= (int)PTR2SIZE(cp2 - line2);
         if (cp2[0] == '=' && cp2[1] == '?' && c > 8)
            isenc |= 2;
         if (PTRCMP(cp + c, >=, *linebuf + *linesize - 2)) {
            size_t diff = PTR2SIZE(cp - *linebuf),
               colondiff = PTR2SIZE(*colon - *linebuf);
            *linebuf = srealloc(*linebuf, *linesize += c + 2);
            cp = &(*linebuf)[diff];
            *colon = &(*linebuf)[colondiff];
         }
         if (isenc != 3)
            *cp++ = ' ';
         memcpy(cp, cp2, c);
         cp += c;
      }
      *cp = '\0';

      if (line2 != NULL)
         free(line2);
      break;
   }
   NYD_LEAVE;
   return rem;
}

static int
msgidnextc(char const **cp, int *status)
{
   int c;
   NYD_ENTER;

   for (;;) {
      if (*status & 01) {
         if (**cp == '"') {
            *status &= ~01;
            (*cp)++;
            continue;
         }
         if (**cp == '\\') {
            (*cp)++;
            if (**cp == '\0')
               goto jeof;
         }
         goto jdfl;
      }
      switch (**cp) {
      case '(':
         *cp = skip_comment(&(*cp)[1]);
         continue;
      case '>':
      case '\0':
jeof:
         c = '\0';
         goto jleave;
      case '"':
         (*cp)++;
         *status |= 01;
         continue;
      case '@':
         *status |= 02;
         /*FALLTHRU*/
      default:
jdfl:
         c = *(*cp)++ & 0377;
         c = (*status & 02) ? lowerconv(c) : c;
         goto jleave;
      }
   }
jleave:
   NYD_LEAVE;
   return c;
}

static int
charcount(char *str, int c)
{
   char *cp;
   int i;
   NYD_ENTER;

   for (i = 0, cp = str; *cp; ++cp)
      if (*cp == c)
         ++i;
   NYD_LEAVE;
   return i;
}

static char const *
nexttoken(char const *cp)
{
   NYD_ENTER;
   for (;;) {
      if (*cp == '\0') {
         cp = NULL;
         break;
      }

      if (*cp == '(') {
         size_t nesting = 1;

         do switch (*++cp) {
         case '(':
            ++nesting;
            break;
         case ')':
            --nesting;
            break;
         } while (nesting > 0 && *cp != '\0'); /* XXX error? */
      } else if (blankchar(*cp) || *cp == ',')
         ++cp;
      else
         break;
   }
   NYD_LEAVE;
   return cp;
}

FL char const *
myaddrs(struct header *hp)
{
   struct name *np;
   char *rv;
   NYD_ENTER;

   if (hp != NULL && (np = hp->h_from) != NULL) {
      if ((rv = np->n_fullname) != NULL)
         goto jleave;
      if ((rv = np->n_name) != NULL)
         goto jleave;
   }

   if ((rv = ok_vlook(from)) != NULL)
      goto jleave;

   /* When invoking *sendmail* directly, it's its task to generate an otherwise
    * undeterminable From: address.  However, if the user sets *hostname*,
    * accept his desire */
   if (ok_vlook(smtp) != NULL || ok_vlook(hostname) != NULL) {
      char *hn = nodename(1);
      size_t sz = strlen(myname) + strlen(hn) + 1 +1;
      rv = salloc(sz);
      sstpcpy(sstpcpy(sstpcpy(rv, myname), "@"), hn);
   }
jleave:
   NYD_LEAVE;
   return rv;
}

FL char const *
myorigin(struct header *hp)
{
   char const *rv = NULL, *ccp;
   struct name *np;
   NYD_ENTER;

   if ((ccp = myaddrs(hp)) != NULL &&
         (np = lextract(ccp, GEXTRA | GFULL)) != NULL)
      rv = (np->n_flink != NULL) ? ok_vlook(sender) : ccp;
   NYD_LEAVE;
   return rv;
}

FL int
is_head(char const *linebuf, size_t linelen) /* XXX verbose WARN */
{
   char date[FROM_DATEBUF];
   int rv;
   NYD_ENTER;

   rv = ((linelen <= 5 || strncmp(linebuf, "From ", 5) != 0 ||
         !extract_date_from_from_(linebuf, linelen, date) ||
         !_is_date(date)) ? 0 : 1);
   NYD_LEAVE;
   return rv;
}

FL int
extract_date_from_from_(char const *line, size_t linelen,
   char datebuf[FROM_DATEBUF])
{
   int rv = 0;
   char const *cp = line;
   NYD_ENTER;

   /* "From " */
   cp = _from__skipword(cp);
   if (cp == NULL)
      goto jerr;
   /* "addr-spec " */
   cp = _from__skipword(cp);
   if (cp == NULL)
      goto jerr;
   if (cp[0] == 't' && cp[1] == 't' && cp[2] == 'y') {
      cp = _from__skipword(cp);
      if (cp == NULL)
         goto jerr;
   }

   linelen -= PTR2SIZE(cp - line);
   if (linelen < _DATE_MINLEN)
      goto jerr;
   if (cp[linelen - 1] == '\n') {
      --linelen;
      /* (Rather IMAP/POP3 only) */
      if (cp[linelen - 1] == '\r')
         --linelen;
      if (linelen < _DATE_MINLEN)
         goto jerr;
   }
   if (linelen >= FROM_DATEBUF)
      goto jerr;

   rv = 1;
jleave:
   memcpy(datebuf, cp, linelen);
   datebuf[linelen] = '\0';
   NYD_LEAVE;
   return rv;
jerr:
   cp = tr(213, "<Unknown date>");
   linelen = strlen(cp);
   if (linelen >= FROM_DATEBUF)
      linelen = FROM_DATEBUF;
   goto jleave;
}

FL void
extract_header(FILE *fp, struct header *hp) /* XXX no header occur-cnt check */
{
   struct header nh, *hq = &nh;
   char *linebuf = NULL /* TODO line pool */, *colon;
   size_t linesize = 0, seenfields = 0;
   int lc, c;
   char const *val, *cp;
   NYD_ENTER;

   memset(hq, 0, sizeof *hq);
   for (lc = 0; readline_restart(fp, &linebuf, &linesize, 0) > 0; ++lc)
      ;

   /* TODO yippieia, cat(check(lextract)) :-) */
   rewind(fp);
   while ((lc = gethfield(fp, &linebuf, &linesize, lc, &colon)) >= 0) {
      if ((val = thisfield(linebuf, "to")) != NULL) {
         ++seenfields;
         hq->h_to = cat(hq->h_to, checkaddrs(lextract(val, GTO | GFULL)));
      } else if ((val = thisfield(linebuf, "cc")) != NULL) {
         ++seenfields;
         hq->h_cc = cat(hq->h_cc, checkaddrs(lextract(val, GCC | GFULL)));
      } else if ((val = thisfield(linebuf, "bcc")) != NULL) {
         ++seenfields;
         hq->h_bcc = cat(hq->h_bcc, checkaddrs(lextract(val, GBCC | GFULL)));
      } else if ((val = thisfield(linebuf, "from")) != NULL) {
         ++seenfields;
         hq->h_from = cat(hq->h_from,
               checkaddrs(lextract(val, GEXTRA | GFULL)));
      } else if ((val = thisfield(linebuf, "reply-to")) != NULL) {
         ++seenfields;
         hq->h_replyto = cat(hq->h_replyto,
               checkaddrs(lextract(val, GEXTRA | GFULL)));
      } else if ((val = thisfield(linebuf, "sender")) != NULL) {
         ++seenfields;
         hq->h_sender = cat(hq->h_sender,
               checkaddrs(lextract(val, GEXTRA | GFULL)));
      } else if ((val = thisfield(linebuf, "organization")) != NULL) {
         ++seenfields;
         for (cp = val; blankchar(*cp); ++cp)
            ;
         hq->h_organization = (hq->h_organization != NULL)
               ? save2str(hq->h_organization, cp) : savestr(cp);
      } else if ((val = thisfield(linebuf, "subject")) != NULL ||
            (val = thisfield(linebuf, "subj")) != NULL) {
         ++seenfields;
         for (cp = val; blankchar(*cp); ++cp)
            ;
         hq->h_subject = (hq->h_subject != NULL)
               ? save2str(hq->h_subject, cp) : savestr(cp);
      } else
         fprintf(stderr, tr(266, "Ignoring header field \"%s\"\n"), linebuf);
   }

   /* In case the blank line after the header has been edited out.  Otherwise,
    * fetch the header separator */
   if (linebuf != NULL) {
      if (linebuf[0] != '\0') {
         for (cp = linebuf; *(++cp) != '\0';)
            ;
         fseek(fp, (long)-PTR2SIZE(1 + cp - linebuf), SEEK_CUR);
      } else {
         if ((c = getc(fp)) != '\n' && c != EOF)
            ungetc(c, fp);
      }
   }

   if (seenfields > 0) {
      hp->h_to = hq->h_to;
      hp->h_cc = hq->h_cc;
      hp->h_bcc = hq->h_bcc;
      hp->h_from = hq->h_from;
      hp->h_replyto = hq->h_replyto;
      hp->h_sender = hq->h_sender;
      hp->h_organization = hq->h_organization;
      hp->h_subject = hq->h_subject;
   } else
      fprintf(stderr, tr(267, "Restoring deleted header lines\n"));

   if (linebuf != NULL)
      free(linebuf);
   NYD_LEAVE;
}

FL char *
hfield_mult(char const *field, struct message *mp, int mult)
{
   FILE *ibuf;
   int lc;
   size_t linesize = 0; /* TODO line pool */
   char *linebuf = NULL, *colon, *oldhfield = NULL;
   char const *hfield;
   NYD_ENTER;

   if ((ibuf = setinput(&mb, mp, NEED_HEADER)) == NULL)
      goto jleave;
   if ((lc = mp->m_lines - 1) < 0)
      goto jleave;

   if ((mp->m_flag & MNOFROM) == 0 &&
         readline_restart(ibuf, &linebuf, &linesize, 0) < 0)
      goto jleave;
   while (lc > 0) {
      if ((lc = gethfield(ibuf, &linebuf, &linesize, lc, &colon)) < 0)
         break;
      if ((hfield = thisfield(linebuf, field)) != NULL) {
         oldhfield = save2str(hfield, oldhfield);
         if (mult == 0)
            break;
      }
   }

jleave:
   if (linebuf != NULL)
      free(linebuf);
   NYD_LEAVE;
   return oldhfield;
}

FL char const *
thisfield(char const *linebuf, char const *field)
{
   char const *rv = NULL;
   NYD_ENTER;

   while (lowerconv(*linebuf) == lowerconv(*field)) {
      ++linebuf;
      ++field;
   }
   if (*field != '\0')
      goto jleave;

   while (blankchar(*linebuf))
      ++linebuf;
   if (*linebuf++ != ':')
      goto jleave;

   while (blankchar(*linebuf)) /* TODO header parser..  strip trailing WS?!? */
      ++linebuf;
   rv = linebuf;
jleave:
   NYD_LEAVE;
   return rv;
}

FL char *
nameof(struct message *mp, int reptype)
{
   char *cp, *cp2;
   NYD_ENTER;

   cp = skin(name1(mp, reptype));
   if (reptype != 0 || charcount(cp, '!') < 2)
      goto jleave;
   cp2 = strrchr(cp, '!');
   --cp2;
   while (cp2 > cp && *cp2 != '!')
      --cp2;
   if (*cp2 == '!')
      cp = cp2 + 1;
jleave:
   NYD_LEAVE;
   return cp;
}

FL char const *
skip_comment(char const *cp)
{
   size_t nesting;
   NYD_ENTER;

   for (nesting = 1; nesting > 0 && *cp; ++cp) {
      switch (*cp) {
      case '\\':
         if (cp[1])
            ++cp;
         break;
      case '(':
         ++nesting;
         break;
      case ')':
         --nesting;
         break;
      }
   }
   NYD_LEAVE;
   return cp;
}

FL char const *
routeaddr(char const *name)
{
   char const *np, *rp = NULL;
   NYD_ENTER;

   for (np = name; *np; np++) {
      switch (*np) {
      case '(':
         np = skip_comment(np + 1) - 1;
         break;
      case '"':
         while (*np) {
            if (*++np == '"')
               break;
            if (*np == '\\' && np[1])
               np++;
         }
         break;
      case '<':
         rp = np;
         break;
      case '>':
         goto jleave;
      }
   }
   rp = NULL;
jleave:
   NYD_LEAVE;
   return rp;
}

FL int
is_addr_invalid(struct name *np, int putmsg)
{
   char cbuf[sizeof "'\\U12340'"], *name;
   int f, ok8bit;
   ui32_t c;
   char const *fmt, *cs;
   NYD_ENTER;

   name = np->n_name;
   f = np->n_flags;
   ok8bit = 1;
   fmt = "'\\x%02X'";

   if (!(f & NAME_ADDRSPEC_INVALID) || !putmsg || (f & NAME_ADDRSPEC_ERR_EMPTY))
      goto jleave;

   if (f & NAME_ADDRSPEC_ERR_IDNA)
      cs = tr(284, "Invalid domain name: \"%s\", character %s\n"),
      fmt = "'\\U%04X'",
      ok8bit = 0;
   else if (f & NAME_ADDRSPEC_ERR_ATSEQ)
      cs = tr(142, "\"%s\" contains invalid %s sequence\n");
   else
      cs = tr(143, "\"%s\" contains invalid character %s\n");

   c = NAME_ADDRSPEC_ERR_GETWC(f);
   if (ok8bit && c >= 040 && c <= 0177)
      snprintf(cbuf, sizeof cbuf, "'%c'", c);
   else
      snprintf(cbuf, sizeof cbuf, fmt, c);

   fprintf(stderr, cs, name, cbuf);
jleave:
   NYD_LEAVE;
   return ((f & NAME_ADDRSPEC_INVALID) != 0);
}

FL char *
skin(char const *name)
{
   struct addrguts ag;
   char *ret = NULL;
   NYD_ENTER;

   if (name != NULL) {
      addrspec_with_guts(1, name, &ag);
      ret = ag.ag_skinned;
      if (!(ag.ag_n_flags & NAME_NAME_SALLOC))
         ret = savestrbuf(ret, ag.ag_slen);
   }
   NYD_LEAVE;
   return ret;
}

/* TODO addrspec_with_guts: RFC 5322 */
FL int
addrspec_with_guts(int doskin, char const *name, struct addrguts *agp)
{
   char const *cp;
   char *cp2, *bufend, *nbuf, c, gotlt, gotaddr, lastsp;
   int rv = 1;
   NYD_ENTER;

   memset(agp, 0, sizeof *agp);

   if ((agp->ag_input = name) == NULL || (agp->ag_ilen = strlen(name)) == 0) {
      agp->ag_skinned = UNCONST(""); /* ok: NAME_SALLOC is not set */
      agp->ag_slen = 0;
      agp->ag_n_flags |= NAME_ADDRSPEC_CHECKED;
      NAME_ADDRSPEC_ERR_SET(agp->ag_n_flags, NAME_ADDRSPEC_ERR_EMPTY, 0);
      goto jleave;
   }

   if (!doskin || !anyof(name, "(< ")) {
      /*agp->ag_iaddr_start = 0;*/
      agp->ag_iaddr_aend = agp->ag_ilen;
      agp->ag_skinned = UNCONST(name); /* (NAME_SALLOC not set) */
      agp->ag_slen = agp->ag_ilen;
      agp->ag_n_flags = NAME_SKINNED;
      goto jcheck;
   }

   /* Something makes us think we have to perform the skin operation */
   nbuf = ac_alloc(agp->ag_ilen + 1);
   /*agp->ag_iaddr_start = 0;*/
   cp2 = bufend = nbuf;
   gotlt = gotaddr = lastsp = 0;

   for (cp = name++; (c = *cp++) != '\0'; ) {
      switch (c) {
      case '(':
         cp = skip_comment(cp);
         lastsp = 0;
         break;
      case '"':
         /* Start of a "quoted-string".
          * Copy it in its entirety */
         /* XXX RFC: quotes are "semantically invisible"
          * XXX But it was explicitly added (Changelog.Heirloom,
          * XXX [9.23] released 11/15/00, "Do not remove quotes
          * XXX when skinning names"?  No more info.. */
         *cp2++ = c;
         while ((c = *cp) != '\0') { /* TODO improve */
            cp++;
            if (c == '"') {
               *cp2++ = c;
               break;
            }
            if (c != '\\')
               *cp2++ = c;
            else if ((c = *cp) != '\0') {
               *cp2++ = c;
               cp++;
            }
         }
         lastsp = 0;
         break;
      case ' ':
      case '\t':
         if (gotaddr == 1) {
            gotaddr = 2;
            agp->ag_iaddr_aend = PTR2SIZE(cp - name);
         }
         if (cp[0] == 'a' && cp[1] == 't' && blankchar(cp[2]))
            cp += 3, *cp2++ = '@';
         else if (cp[0] == '@' && blankchar(cp[1]))
            cp += 2, *cp2++ = '@';
         else
            lastsp = 1;
         break;
      case '<':
         agp->ag_iaddr_start = PTR2SIZE(cp - (name - 1));
         cp2 = bufend;
         gotlt = gotaddr = 1;
         lastsp = 0;
         break;
      case '>':
         if (gotlt) {
            /* (_addrspec_check() verifies these later!) */
            agp->ag_iaddr_aend = PTR2SIZE(cp - name);
            gotlt = 0;
            while ((c = *cp) != '\0' && c != ',') {
               cp++;
               if (c == '(')
                  cp = skip_comment(cp);
               else if (c == '"')
                  while ((c = *cp) != '\0') {
                     cp++;
                     if (c == '"')
                        break;
                     if (c == '\\' && *cp != '\0')
                        ++cp;
                  }
            }
            lastsp = 0;
            break;
         }
         /* FALLTRHOUGH */
      default:
         if (lastsp) {
            lastsp = 0;
            if (gotaddr)
               *cp2++ = ' ';
         }
         *cp2++ = c;
         if (c == ',') {
            if (!gotlt) {
               *cp2++ = ' ';
               for (; blankchar(*cp); ++cp)
                  ;
               lastsp = 0;
               bufend = cp2;
            }
         } else if (!gotaddr) {
            gotaddr = 1;
            agp->ag_iaddr_start = PTR2SIZE(cp - name);
         }
      }
   }
   agp->ag_slen = PTR2SIZE(cp2 - nbuf);
   if (agp->ag_iaddr_aend == 0)
      agp->ag_iaddr_aend = agp->ag_ilen;

   agp->ag_skinned = savestrbuf(nbuf, agp->ag_slen);
   ac_free(nbuf);
   agp->ag_n_flags = NAME_NAME_SALLOC | NAME_SKINNED;
jcheck:
   rv = _addrspec_check(doskin, agp);
jleave:
   NYD_LEAVE;
   return rv;
}

FL char *
realname(char const *name)
{
   char const *cp, *cq, *cstart = NULL, *cend = NULL;
   char *rname, *rp;
   struct str in, out;
   int quoted, good, nogood;
   NYD_ENTER;

   if ((cp = UNCONST(name)) == NULL)
      goto jleave;
   for (; *cp != '\0'; ++cp) {
      switch (*cp) {
      case '(':
         if (cstart != NULL) {
            /* More than one comment in address, doesn't make sense to display
             * it without context.  Return the entire field */
            cp = mime_fromaddr(name);
            goto jleave;
         }
         cstart = cp++;
         cp = skip_comment(cp);
         cend = cp--;
         if (cend <= cstart)
            cend = cstart = NULL;
         break;
      case '"':
         while (*cp) {
            if (*++cp == '"')
               break;
            if (*cp == '\\' && cp[1])
               ++cp;
         }
         break;
      case '<':
         if (cp > name) {
            cstart = name;
            cend = cp;
         }
         break;
      case ',':
         /* More than one address. Just use the first one */
         goto jbrk;
      }
   }

jbrk:
   if (cstart == NULL) {
      if (*name == '<') {
         /* If name contains only a route-addr, the surrounding angle brackets
          * don't serve any useful purpose when displaying, so remove */
         cp = prstr(skin(name));
      } else
         cp = mime_fromaddr(name);
      goto jleave;
   }

   /* Strip quotes. Note that quotes that appear within a MIME encoded word are
    * not stripped. The idea is to strip only syntactical relevant things (but
    * this is not necessarily the most sensible way in practice) */
   rp = rname = ac_alloc(PTR2SIZE(cend - cstart +1));
   quoted = 0;
   for (cp = cstart; cp < cend; ++cp) {
      if (*cp == '(' && !quoted) {
         cq = skip_comment(++cp);
         if (PTRCMP(--cq, >, cend))
            cq = cend;
         while (cp < cq) {
            if (*cp == '\\' && PTRCMP(cp + 1, <, cq))
               ++cp;
            *rp++ = *cp++;
         }
      } else if (*cp == '\\' && PTRCMP(cp + 1, <, cend))
         *rp++ = *++cp;
      else if (*cp == '"') {
         quoted = !quoted;
         continue;
      } else
         *rp++ = *cp;
   }
   *rp = '\0';
   in.s = rname;
   in.l = rp - rname;
   mime_fromhdr(&in, &out, TD_ISPR | TD_ICONV);
   ac_free(rname);
   rname = savestr(out.s);
   free(out.s);

   while (blankchar(*rname))
      ++rname;
   for (rp = rname; *rp != '\0'; ++rp)
      ;
   while (PTRCMP(--rp, >=, rname) && blankchar(*rp))
      *rp = '\0';
   if (rp == rname) {
      cp = mime_fromaddr(name);
      goto jleave;
   }

   /* mime_fromhdr() has converted all nonprintable characters to question
    * marks now. These and blanks are considered uninteresting; if the
    * displayed part of the real name contains more than 25% of them, it is
    * probably better to display the plain email address instead */
   good = 0;
   nogood = 0;
   for (rp = rname; *rp != '\0' && PTRCMP(rp, <, rname + 20); ++rp)
      if (*rp == '?' || blankchar(*rp))
         ++nogood;
      else
         ++good;
   cp = (good * 3 < nogood) ? prstr(skin(name)) : rname;
jleave:
   NYD_LEAVE;
   return UNCONST(cp);
}

FL char *
name1(struct message *mp, int reptype)
{
   char *namebuf, *cp, *cp2, *linebuf = NULL /* TODO line pool */;
   size_t namesize, linesize = 0;
   FILE *ibuf;
   int f1st = 1;
   NYD_ENTER;

   if ((cp = hfield1("from", mp)) != NULL && *cp != '\0')
      goto jleave;
   if (reptype == 0 && (cp = hfield1("sender", mp)) != NULL && *cp != '\0')
      goto jleave;

   namebuf = smalloc(namesize = 1);
   namebuf[0] = 0;
   if (mp->m_flag & MNOFROM)
      goto jout;
   if ((ibuf = setinput(&mb, mp, NEED_HEADER)) == NULL)
      goto jout;
   if (readline_restart(ibuf, &linebuf, &linesize, 0) < 0)
      goto jout;

jnewname:
   if (namesize <= linesize)
      namebuf = srealloc(namebuf, namesize = linesize +1);
   for (cp = linebuf; *cp != '\0' && *cp != ' '; ++cp)
      ;
   for (; blankchar(*cp); ++cp)
      ;
   for (cp2 = namebuf + strlen(namebuf);
        *cp && !blankchar(*cp) && PTRCMP(cp2, <, namebuf + namesize -1);)
      *cp2++ = *cp++;
   *cp2 = '\0';

   if (readline_restart(ibuf, &linebuf, &linesize, 0) < 0)
      goto jout;
   if ((cp = strchr(linebuf, 'F')) == NULL)
      goto jout;
   if (strncmp(cp, "From", 4)) /* XXX is_head? */
      goto jout;
   if (namesize <= linesize)
      namebuf = srealloc(namebuf, namesize = linesize + 1);

   while ((cp = strchr(cp, 'r')) != NULL) {
      if (!strncmp(cp, "remote", 6)) {
         if ((cp = strchr(cp, 'f')) == NULL)
            break;
         if (strncmp(cp, "from", 4) != 0)
            break;
         if ((cp = strchr(cp, ' ')) == NULL)
            break;
         cp++;
         if (f1st) {
            strncpy(namebuf, cp, namesize);
            f1st = 0;
         } else {
            cp2 = strrchr(namebuf, '!') + 1;
            strncpy(cp2, cp, PTR2SIZE(namebuf + namesize - cp2));
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

   if (linebuf != NULL)
      free(linebuf);
   free(namebuf);
jleave:
   NYD_LEAVE;
   return cp;
}

FL int
msgidcmp(char const *s1, char const *s2)
{
   int q1 = 0, q2 = 0, c1, c2;
   NYD_ENTER;

   do {
      c1 = msgidnextc(&s1, &q1);
      c2 = msgidnextc(&s2, &q2);
      if (c1 != c2)
         break;
   } while (c1 && c2);
   NYD_LEAVE;
   return c1 - c2;
}

FL int
is_ign(char const *field, size_t fieldlen, struct ignoretab ignoret[2])
{
   char *realfld;
   int rv;
   NYD_ENTER;

   rv = 0;
   if (ignoret == NULL)
      goto jleave;
   rv = 1;
   if (ignoret == allignore)
      goto jleave;

   /* Lowercase it so that "Status" and "status" will hash to the same place */
   realfld = ac_alloc(fieldlen +1);
   i_strcpy(realfld, field, fieldlen +1);
   if (ignoret[1].i_count > 0)
      rv = !member(realfld, ignoret + 1);
   else
      rv = member(realfld, ignoret);
   ac_free(realfld);
jleave:
   NYD_LEAVE;
   return rv;
}

FL int
member(char const *realfield, struct ignoretab *table)
{
   struct ignore *igp;
   int rv = 0;
   NYD_ENTER;

   for (igp = table->i_head[hash(realfield)]; igp != 0; igp = igp->i_link)
      if (*igp->i_field == *realfield && !strcmp(igp->i_field, realfield)) {
         rv = 1;
         break;
      }
   NYD_LEAVE;
   return rv;
}

FL char const *
fakefrom(struct message *mp)
{
   char const *name;
   NYD_ENTER;

   if (((name = skin(hfield1("return-path", mp))) == NULL || *name == '\0' ) &&
         ((name = skin(hfield1("from", mp))) == NULL || *name == '\0'))
      /* XXX MAILER-DAEMON is what an old MBOX manual page says.
       * RFC 4155 however requires a RFC 5322 (2822) conforming
       * "addr-spec", but we simply can't provide that */
      name = "MAILER-DAEMON";
   NYD_LEAVE;
   return name;
}

FL char const *
fakedate(time_t t)
{
   char *cp, *cq;
   NYD_ENTER;

   cp = ctime(&t);
   for (cq = cp; *cq != '\0' && *cq != '\n'; ++cq)
      ;
   *cq = '\0';
   cp = savestr(cp);
   NYD_LEAVE;
   return cp;
}

FL time_t
unixtime(char const *fromline)
{
   char const *fp;
   char *xp;
   time_t t;
   int i, year, month, day, hour, minute, second, tzdiff;
   struct tm *tmptr;
   NYD_ENTER;

   for (fp = fromline; *fp != '\0' && *fp != '\n'; ++fp)
      ;
   fp -= 24;
   if (PTR2SIZE(fp - fromline) < 7)
      goto jinvalid;
   if (fp[3] != ' ')
      goto jinvalid;
   for (i = 0;;) {
      if (!strncmp(fp + 4, month_names[i], 3))
         break;
      if (month_names[++i][0] == '\0')
         goto jinvalid;
   }
   month = i + 1;
   if (fp[7] != ' ')
      goto jinvalid;
   day = strtol(fp + 8, &xp, 10);
   if (*xp != ' ' || xp != fp + 10)
      goto jinvalid;
   hour = strtol(fp + 11, &xp, 10);
   if (*xp != ':' || xp != fp + 13)
      goto jinvalid;
   minute = strtol(fp + 14, &xp, 10);
   if (*xp != ':' || xp != fp + 16)
      goto jinvalid;
   second = strtol(fp + 17, &xp, 10);
   if (*xp != ' ' || xp != fp + 19)
      goto jinvalid;
   year = strtol(fp + 20, &xp, 10);
   if (xp != fp + 24)
      goto jinvalid;
   if ((t = combinetime(year, month, day, hour, minute, second)) == (time_t)-1)
      goto jinvalid;
   tzdiff = t - mktime(gmtime(&t));
   tmptr = localtime(&t);
   if (tmptr->tm_isdst > 0)
      tzdiff += 3600;
   t -= tzdiff;
jleave:
   NYD_LEAVE;
   return t;
jinvalid:
   time(&t);
   goto jleave;
}

FL time_t
rfctime(char const *date)
{
   char const *cp = date;
   char *x;
   time_t t;
   int i, year, month, day, hour, minute, second;
   NYD_ENTER;

   if ((cp = nexttoken(cp)) == NULL)
      goto jinvalid;
   if (alphachar(cp[0]) && alphachar(cp[1]) && alphachar(cp[2]) &&
         cp[3] == ',') {
      if ((cp = nexttoken(&cp[4])) == NULL)
         goto jinvalid;
   }
   day = strtol(cp, &x, 10); /* XXX strtol */
   if ((cp = nexttoken(x)) == NULL)
      goto jinvalid;
   for (i = 0;;) {
      if (!strncmp(cp, month_names[i], 3))
         break;
      if (month_names[++i][0] == '\0')
         goto jinvalid;
   }
   month = i + 1;
   if ((cp = nexttoken(&cp[3])) == NULL)
      goto jinvalid;
   /* RFC 5322, 4.3:
    *  Where a two or three digit year occurs in a date, the year is to be
    *  interpreted as follows: If a two digit year is encountered whose
    *  value is between 00 and 49, the year is interpreted by adding 2000,
    *  ending up with a value between 2000 and 2049.  If a two digit year
    *  is encountered with a value between 50 and 99, or any three digit
    *  year is encountered, the year is interpreted by adding 1900 */
   year = strtol(cp, &x, 10); /* XXX strtol */
   i = (int)PTR2SIZE(x - cp);
   if (i == 2 && year >= 0 && year <= 49)
      year += 2000;
   else if (i == 3 || (i == 2 && year >= 50 && year <= 99))
      year += 1900;
   if ((cp = nexttoken(x)) == NULL)
      goto jinvalid;
   hour = strtol(cp, &x, 10); /* XXX strtol */
   if (*x != ':')
      goto jinvalid;
   cp = &x[1];
   minute = strtol(cp, &x, 10);
   if (*x == ':') {
      cp = x + 1;
      second = strtol(cp, &x, 10);
   } else
      second = 0;
   if ((t = combinetime(year, month, day, hour, minute, second)) == (time_t)-1)
      goto jinvalid;
   if ((cp = nexttoken(x)) != NULL) {
      int sign = -1;
      char buf[3];

      switch (*cp) {
      case '-':
         sign = 1;
         /*FALLTHRU*/
      case '+':
         ++cp;
         break;
      }
      if (digitchar(cp[0]) && digitchar(cp[1]) && digitchar(cp[2]) &&
            digitchar(cp[3])) {
         buf[2] = '\0';
         buf[0] = cp[0];
         buf[1] = cp[1];
         t += strtol(buf, NULL, 10) * sign * 3600;/*XXX strtrol*/
         buf[0] = cp[2];
         buf[1] = cp[3];
         t += strtol(buf, NULL, 10) * sign * 60; /* XXX strtol*/
      }
      /* TODO WE DO NOT YET PARSE (OBSOLETE) ZONE NAMES
       * TODO once again, Christos Zoulas and NetBSD Mail have done
       * TODO a really good job already, but using strptime(3), which
       * TODO is not portable.  Nonetheless, WE must improve, not
       * TODO at last because we simply ignore obsolete timezones!!
       * TODO See RFC 5322, 4.3! */
   }
jleave:
   NYD_LEAVE;
   return t;
jinvalid:
   t = 0;
   goto jleave;
}

#define is_leapyear(Y)  ((((Y) % 100 ? (Y) : (Y) / 100) & 3) == 0)

FL time_t
combinetime(int year, int month, int day, int hour, int minute, int second)
{
   time_t t;
   NYD_ENTER;

   if (second < 0 || minute < 0 || hour < 0 || day < 1) {
      t = (time_t)-1;
      goto jleave;
   }

   t = second + minute * 60 + hour * 3600 + (day - 1) * 86400;
   if (month > 1)
      t += 86400 * 31;
   if (month > 2)
      t += 86400 * (is_leapyear(year) ? 29 : 28);
   if (month > 3)
      t += 86400 * 31;
   if (month > 4)
      t += 86400 * 30;
   if (month > 5)
      t += 86400 * 31;
   if (month > 6)
      t += 86400 * 30;
   if (month > 7)
      t += 86400 * 31;
   if (month > 8)
      t += 86400 * 31;
   if (month > 9)
      t += 86400 * 30;
   if (month > 10)
      t += 86400 * 31;
   if (month > 11)
      t += 86400 * 30;
   year -= 1900;
   t += (year - 70) * 31536000 + ((year - 69) / 4) * 86400 -
         ((year - 1) / 100) * 86400 + ((year + 299) / 400) * 86400;
jleave:
   NYD_LEAVE;
   return t;
}

FL void
substdate(struct message *m)
{
   char const *cp;
   NYD_ENTER;

   /* Determine the date to print in faked 'From ' lines. This is traditionally
    * the date the message was written to the mail file. Try to determine this
    * using RFC message header fields, or fall back to current time */
   if ((cp = hfield1("received", m)) != NULL) {
      while ((cp = nexttoken(cp)) != NULL && *cp != ';') {
         do
            ++cp;
         while (alnumchar(*cp));
      }
      if (cp && *++cp)
         m->m_time = rfctime(cp);
   }
   if (m->m_time == 0 || m->m_time > time_current.tc_time) {
      if ((cp = hfield1("date", m)) != NULL)
         m->m_time = rfctime(cp);
   }
   if (m->m_time == 0 || m->m_time > time_current.tc_time)
      m->m_time = time_current.tc_time;
   NYD_LEAVE;
}

FL int
check_from_and_sender(struct name *fromfield, struct name *senderfield)
{
   int rv;
   NYD_ENTER;

   if (fromfield && fromfield->n_flink && senderfield == NULL) {
      fprintf(stderr, tr(529, "A Sender: field is required with multiple "
         "addresses in From: field.\n"));
      rv = 1;
   } else if (senderfield && senderfield->n_flink) {
      fprintf(stderr, tr(530,
         "The Sender: field may contain only one address.\n"));
      rv = 2;
   } else
      rv = 0;
   NYD_LEAVE;
   return rv;
}

FL char *
getsender(struct message *mp)
{
   char *cp;
   struct name *np;
   NYD_ENTER;

   if ((cp = hfield1("from", mp)) == NULL ||
         (np = lextract(cp, GEXTRA | GSKIN)) == NULL)
      cp = NULL;
   else
      cp = (np->n_flink != NULL) ? skin(hfield1("sender", mp)) : np->n_name;
   NYD_LEAVE;
   return cp;
}

FL int
grab_headers(struct header *hp, enum gfield gflags, int subjfirst)
{
   /* TODO grab_headers: again, check counts etc. against RFC;
    * TODO (now assumes check_from_and_sender() is called afterwards ++ */
   int errs;
   int volatile comma;
   NYD_ENTER;

   errs = 0;
   comma = (ok_blook(bsdcompat) || ok_blook(bsdmsgs)) ? 0 : GCOMMA;

   if (gflags & GTO)
      hp->h_to = grab_names("To: ", hp->h_to, comma, GTO | GFULL);
   if (subjfirst && (gflags & GSUBJECT))
      hp->h_subject = readstr_input("Subject: ", hp->h_subject);
   if (gflags & GCC)
      hp->h_cc = grab_names("Cc: ", hp->h_cc, comma, GCC | GFULL);
   if (gflags & GBCC)
      hp->h_bcc = grab_names("Bcc: ", hp->h_bcc, comma, GBCC | GFULL);

   if (gflags & GEXTRA) {
      if (hp->h_from == NULL)
         hp->h_from = lextract(myaddrs(hp), GEXTRA | GFULL);
      hp->h_from = grab_names("From: ", hp->h_from, comma, GEXTRA | GFULL);
      if (hp->h_replyto == NULL)
         hp->h_replyto = lextract(ok_vlook(replyto), GEXTRA | GFULL);
      hp->h_replyto = grab_names("Reply-To: ", hp->h_replyto, comma,
            GEXTRA | GFULL);
      if (hp->h_sender == NULL)
         hp->h_sender = extract(ok_vlook(sender), GEXTRA | GFULL);
      hp->h_sender = grab_names("Sender: ", hp->h_sender, comma,
            GEXTRA | GFULL);
      if (hp->h_organization == NULL)
         hp->h_organization = ok_vlook(ORGANIZATION);
      hp->h_organization = readstr_input("Organization: ", hp->h_organization);
   }

   if (!subjfirst && (gflags & GSUBJECT))
      hp->h_subject = readstr_input("Subject: ", hp->h_subject);

   NYD_LEAVE;
   return errs;
}

/* vim:set fenc=utf-8:s-it-mode */
