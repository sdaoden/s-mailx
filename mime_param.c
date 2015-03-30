/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ MIME parameter handling.
 *
 * Copyright (c) 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

struct rfc2231_joiner {
   struct rfc2231_joiner *rj_next;
   ui32_t      rj_no;            /* Continuation number */
   ui32_t      rj_len;           /* of useful data in .rj_dat */
   ui32_t      rj_val_off;       /* Start of value data therein */
   ui32_t      rj_cs_len;        /* Length of charset part */
   bool_t      rj_is_enc;        /* Is percent encoded */
   ui8_t       __pad[7];
   char const  *rj_dat;
};

/* In a headerbody, at a "param=XY" that we're not interested in, skip over the
 * entire construct, return pointer to the first byte thereafter or to NUL */
static char const * _mime_param_skip(char const *hbp);

/* Trim value, which points to after the "name[RFC 2231 stuff]=".
 * On successful return (1,-1; -1 is returned if the value was quoted via
 * double quotation marks) a set end_or_null points to after the value and any
 * possible separator and result->s is the salloc()d normalized value */
static si8_t     _mime_param_value_trim(struct str *result, char const *start,
                     char const **end_or_null);

/* mime_param_get() found the desired parameter but it seems to use RFC 2231
 * extended syntax: perform full RFC 2231 parsing starting at this point.
 * Note that _join() returns is-error */
static char *     _rfc2231_param_parse(char const *param, size_t plen,
                     char const *hbp);
static bool_t     __rfc2231_join(struct rfc2231_joiner *head, char **result,
                     char const **emsg);

static char const *
_mime_param_skip(char const *hbp)
{
   char co, cn;
   NYD2_ENTER;

   /* Skip over parameter name - note we may have skipped over an entire
    * parameter name and thus point to a "="; i haven't yet truly checked
    * against MIME RFCs, just test for ";" in the meanwhile XXX */
   while ((cn = *hbp) != '\0' && cn != '=' && cn != ';')
      ++hbp;
   if (cn == '\0')
      goto jleave;
   ++hbp;
   if (cn == ';')
      goto jleave;

   while (whitechar((cn = *hbp))) /* XXX */
      ++hbp;
   if (cn == '\0')
      goto jleave;

   if (cn == '"') {
      co = '\0';
      while ((cn = *++hbp) != '\0' && (cn != '"' || co == '\\'))
         co = (co == '\\') ? '\0' : cn;
      if (cn != '\0' && (cn = *++hbp) == ';')
         ++hbp;
   } else {
      for (;; cn = *++hbp)
         if (cn == '\0' || cn == ';' || whitechar(cn))
            break;
      if (cn != '\0')
         ++hbp;
   }
jleave:
   NYD2_LEAVE;
   return hbp;
}

static si8_t
_mime_param_value_trim(struct str *result, char const *start,
   char const **end_or_null)
{
   char const *e;
   char co, cn;
   size_t i;
   si8_t rv;
   NYD2_ENTER;

   while (whitechar(*start)) /* XXX? */
      ++start;

   if (*start == '"') {
      for (co = '\0', e = ++start;; ++e)
         if ((cn = *e) == '\0')
            goto jerr;
         else if (cn == '"' && co != '\\')
            break;
         else if (cn == '\\' && co == '\\')
            co = '\0';
         else
            co = cn;
      i = PTR2SIZE(e++ - start);
      rv = -TRU1;
   } else {
      for (e = start; (cn = *e) != '\0' && !whitechar(cn) && cn != ';'; ++e)
         ;
      i = PTR2SIZE(e - start);
      rv = TRU1;
   }

   result->s = salloc(i +1);
   if (rv > 0) {
      memcpy(result->s, start, result->l = i);
      result->s[i] = '\0';
   } else {
      size_t j;
      char *cp;

      for (j = 0, cp = result->s, co = '\0'; i-- > 0; co = cn) {
         cn = *start++;
         if (cn != '\\' || co == '\\') {
            cp[j++] = cn;
            if (cn == '\\')
               cn = '\0';
         }
      }
      cp[j] = '\0';

      result->s = cp;
      result->l = j;
   }

   if (end_or_null != NULL) {
      while (*e != '\0' && *e == ';')
         ++e;
      *end_or_null = e;
   }
jleave:
   NYD2_LEAVE;
   return rv;
jerr:
   rv = FAL0;
   goto jleave;
}

static char *
_rfc2231_param_parse(char const *param, size_t plen, char const *hbp)
{
   /* TODO Do it for real and unite with mime_param_get() */
   struct str xval;
   char nobuf[32], *eptr, *rv = NULL, c;
   char const *hbp_base, *cp, *emsg = NULL;
   struct rfc2231_joiner *head = NULL, *np;
   bool_t errors = FAL0;
   size_t i;
   NYD2_ENTER;

   /* We were called by mime_param_get() after a param name match that
    * involved "*", so jump to the matching code */
   hbp_base = hbp;
   goto jumpin;

   for (; *hbp != '\0'; hbp_base = hbp) {
      while (whitechar(*hbp))
         ++hbp;

      if (!ascncasecmp(hbp, param, plen)) {
         hbp += plen;
         while (whitechar(*hbp))
            ++hbp;
         if (*hbp++ != '*')
               goto jerr;

         /* RFC 2231 extensions: "NAME[*DIGITS][*]=", where "*DIGITS" indicates
          * parameter continuation and the lone asterisk "*" percent encoded
          * values -- if encoding is used the "*0" or lone parameter value
          * MUST be encoded and start with a "CHARSET'LANGUAGE'" construct,
          * where both of CHARSET and LANGUAGE are optional (we do effectively
          * generate error if CHARSET is missing though).
          * Continuations may not use that "C'L'" construct, but be tolerant
          * and ignore those.  Also encoded and non-encoded continuations may
          * occur, i.e., perform percent en-/decoding only as necessary.
          * Continuations may occur in any order */
         /* xxx RFC 2231 parsing ignores language tags */
jumpin:
         for (cp = hbp; digitchar(*cp); ++cp)
            ;
         i = PTR2SIZE(cp - hbp);
         if (i != 0) {
            if (i >= sizeof(nobuf)) {
               emsg = N_("too many digits to form a valid number");
               goto jerr;
            } else if ((c = *cp) != '=' && c != '*') {
               emsg = N_("expected \"=\" or \"*\" after leading digits");
               goto jerr;
            }
            memcpy(nobuf, hbp, i);
            nobuf[i] = '\0';
            i = (size_t)strtol(nobuf, UNCONST(&eptr), 10);
            if (i >= 999 || *eptr != '\0') {
               emsg = N_("invalid continuation sequence number");
               goto jerr;
            }
            hbp = ++cp;

            /* Value encoded? */
            if (c == '*') {
               if (*hbp++ != '=')
                  goto jeeqaaster;
            } else if (c != '=') {
jeeqaaster:
               emsg = N_("expected \"=\" after asterisk \"*\"");
               goto jerr;
            }
         } else {
            /* In continuation mode that is an error, however */
            if (head != NULL) {
               emsg = N_("missing continuation sequence number");
               goto jerr;
            }
            /* Parameter value is encoded, may define encoding */
            c = '*';
            if (*cp != '=')
               goto jeeqaaster;
            hbp = ++cp;
            i = 0;
         }

         /* Create new node and insert it sorted; should be faster than
          * creating an unsorted list and sorting it after parsing */
         np = smalloc(sizeof *np);
         np->rj_next = NULL;
         np->rj_no = (ui32_t)i;
         np->rj_is_enc = (c == '*');
         np->rj_val_off = np->rj_cs_len = 0;

         if (head == NULL)
            head = np;
         else if (i < head->rj_no) {
            np->rj_next = head;
            head = np;
         } else {
            struct rfc2231_joiner *l = NULL, *x = head;

            while (x != NULL && i > x->rj_no)
               l = x, x = x->rj_next;
            if (x != NULL)
               np->rj_next = x;
            l->rj_next = np;
         }

         switch (_mime_param_value_trim(&xval, hbp, &cp)) {
         default:
            emsg = (c == '*') ? N_("invalid value encoding")/* XXX fake */
                  : N_("faulty value - missing closing quotation mark \"\"\"?");
            goto jerr;
         case -1:
            /* XXX if (np->is_enc && memchr(np->dat, '\'', i) != NULL) {
             * XXX    emsg = N_("character set info not allowed here");
             * XXX    goto jerr;
             * XXX } */np->rj_is_enc = FAL0; /* Silently ignore */
            /* FALLTHRU */
         case 1:
            if (xval.l >= UI32_MAX) {
               emsg = N_("parameter value too long");
               goto jerr;
            }
            np->rj_len = (ui32_t)xval.l;
            np->rj_dat = xval.s;
            break;
         }

         /* Watch out for character set and language info */
         if (np->rj_is_enc && (eptr = memchr(xval.s, '\'', xval.l)) != NULL) {
            np->rj_cs_len = PTR2SIZE(eptr - xval.s);
            if ((eptr = memchr(eptr + 1, '\'', xval.l - np->rj_cs_len - 1))
                  == NULL) {
               emsg = N_("faulty RFC 2231 parameter extension");
               goto jerr;
            }
            np->rj_val_off = PTR2SIZE(++eptr - xval.s);
         }

         hbp = cp;
      } else
         hbp = _mime_param_skip(hbp);
   }
   assert(head != NULL); /* (always true due to jumpin:, but..) */

   errors |= __rfc2231_join(head, &rv, &emsg);
   if (errors && (options & OPT_D_VV)) {
      /* TODO 1. we need our error ring; 2. such errors in particular
       * TODO should set global flags so that at the end of an operation
       * TODO (for a message) a summary can be printed: faulty MIME, xy */
      if (emsg == NULL)
         emsg = N_("multiple causes");
      fprintf(stderr, _("Message had MIME errors: %s\n"),
         V_(emsg));
   }
jleave:
   NYD2_LEAVE;
   return rv;

jerr:
   while ((np = head) != NULL) {
      head = np->rj_next;
      free(np);
   }
   if (options & OPT_D_V) {
      if (emsg == NULL)
         emsg = N_("expected asterisk \"*\"");
      fprintf(stderr,
         _("Faulty \"%s\" RFC 2231 MIME parameter value: %s\n   Near: %s\n"),
         param, V_(emsg), hbp_base);
   }
   errors = TRU1;
   rv = NULL;
   goto jleave;
}

static bool_t
__rfc2231_join(struct rfc2231_joiner *head, char **result, char const **emsg)
{
   struct str sin, sou;
   struct rfc2231_joiner *np;
   char const *cp;
   size_t i;
   enum {
      _NONE       = 0,
      _HAVE_ENC   = 1<<0,
      _HAVE_ICONV = 1<<1,
      _SEEN_ANY   = 1<<2,
      _ERRORS     = 1<<3
   } f = _NONE;
   ui32_t no;
#ifdef HAVE_ICONV
   iconv_t fhicd = (iconv_t)-1;/* XXX pacify compiler */
#endif
   NYD2_ENTER;

#ifdef HAVE_ICONV
   if (head->rj_is_enc) {
      char const *tcs;

      f |= _HAVE_ENC;
      if (head->rj_cs_len == 0) {
         /* It is an error if the character set is not set, the language alone
          * cannot convert characters, let aside that we don't use it at all */
         *emsg = N_("MIME RFC 2231 invalidity: missing character set\n");
         f |= _ERRORS;
      } else if (ascncasecmp(tcs = charset_get_lc(),
            head->rj_dat, head->rj_cs_len)) {
         char *cs = ac_alloc(head->rj_cs_len +1);

         memcpy(cs, head->rj_dat, head->rj_cs_len);
         cs[head->rj_cs_len] = '\0';
         if ((fhicd = n_iconv_open(tcs, cs)) != (iconv_t)-1)
            f |= _HAVE_ICONV;
         else {
            *emsg = N_("necessary character set conversion missing");
            f |= _ERRORS;
         }
         ac_free(cs);
      }
   }
#endif

   if (head->rj_no != 0) {
      if (!(f & _ERRORS))
         *emsg = N_("First RFC 2231 parameter value chunk number is not 0");
      f |= _ERRORS;
   }

   for (sou.s = NULL, sou.l = 0, no = 0; (np = head) != NULL; free(np)) {
      head = np->rj_next;

      if (np->rj_no != no++) {
         if (!(f & _ERRORS))
            *emsg = N_("RFC 2231 parameter value chunks are not contiguous");
         f |= _ERRORS;
      }

      /* RFC 2231 allows such info only in the first continuation, and
       * furthermore MUSTs the first to be encoded, then */
      if (/*np->rj_is_enc &&*/ np->rj_val_off > 0 &&
            (f & (_HAVE_ENC | _SEEN_ANY)) != _HAVE_ENC) {
         if (!(f & _ERRORS))
            *emsg = N_("invalid redundant RFC 2231 charset/language ignored");
         f |= _ERRORS;
      }
      f |= _SEEN_ANY;

      i = np->rj_len - np->rj_val_off;
      if (!np->rj_is_enc) {
         n_str_add_buf(&sou, np->rj_dat + np->rj_val_off, i);
         continue;
      }

      /* Always perform percent decoding */
      sin.s = smalloc(i +1);
      sin.l = 0;
      for (cp = np->rj_dat + np->rj_val_off; i > 0;) {
         char c;

         if ((c = *cp++) == '%') {
            si32_t cc;

            if (i < 3 || (cc = mime_hexseq_to_char(cp)) < 0) {
               if (!(f & _ERRORS))
                  *emsg = N_("invalid RFC 2231 percent encoded sequence");
               f |= _ERRORS;
               goto jhex_putc;
            }
            sin.s[sin.l++] = (char)cc;
            cp += 2;
            i -= 3;
         } else {
jhex_putc:
            sin.s[sin.l++] = c;
            --i;
         }
      }
      sin.s[sin.l] = '\0';

      /* And add character set conversion on top as necessary.
       * RFC 2231 is pragmatic: encode only mentions percent encoding and the
       * character set for the entire string ("[no] facility for using more
       * than one character set or language"), therefore "continuations may
       * contain a mixture of encoded and unencoded segments" applies to
       * a contiguous string of a single character set that has been torn in
       * pieces due to space restrictions, and it happened that some pieces
       * didn't need to be percent encoded.
       *
       * _In particular_ it therefore doesn't repeat the RFC 2047 paradigm
       * that encoded-words-are-atomic, meaning that a single character-set
       * conversion run over the final, joined, partially percent-decoded value
       * should be sufficient */
#ifdef HAVE_ICONV
      if (f & _HAVE_ICONV) {
         struct str sio = {NULL, 0}; /* TODO string pool */

         if (n_iconv_str(fhicd, &sio, &sin, NULL, TRU1) != 0) {
            if (!(f & _ERRORS))
               *emsg = N_("character set conversion failed on value");
            f |= _ERRORS;
            n_str_add_buf(&sio, "?", 1);
         }
         free(sin.s);
         sin = sio;
      }
#endif

      n_str_add_buf(&sou, sin.s, sin.l);
      free(sin.s);
   }

#ifdef HAVE_ICONV
   if ((f & _HAVE_ICONV) && /* XXX pacify compiler */ fhicd != (iconv_t)-1)
      n_iconv_close(fhicd);
#endif
   memcpy(*result = salloc(sou.l +1), sou.s, sou.l +1);
   free(sou.s);
   NYD2_LEAVE;
   return ((f & _ERRORS) != 0);
}

FL char *
mime_param_get(char const *param, char const *headerbody) /* TODO rewr. */
{
   struct str xval;
   char *rv = NULL;
   size_t plen;
   char const *p;
   NYD_ENTER;

   plen = strlen(param);
   p = headerbody;

   /* At the beginning of headerbody there is no parameter=value pair xxx */
   if (!whitechar(*p))
      goto jskip1st;

   for (;;) {
      while (whitechar(*p))
         ++p;

      if (!ascncasecmp(p, param, plen)) {
         p += plen;
         while (whitechar(*p)) /* XXX? */
            ++p;
         switch (*p++) {
         case '*':
            rv = _rfc2231_param_parse(param, plen, p);
            goto jleave;
         case '=':
            if (!_mime_param_value_trim(&xval, p, NULL)) {
               /* XXX LOG? */
               goto jleave;
            }
            rv = xval.s;

            /* We do have a result, but some (elder) software (S-nail <v14.8)
             * will use RFC 2047 encoded words in  parameter values, too */
            /* TODO Automatically check wether the value seems to be RFC 2047
             * TODO encwd. -- instead use *rfc2047_parameters* like mutt(1)? */
            if ((p = strstr(rv, "=?")) != NULL && strstr(p, "?=") != NULL) {
               struct str ti, to;

               ti.l = strlen(ti.s = rv);
               mime_fromhdr(&ti, &to, TD_ISPR | TD_ICONV | TD_DELCTRL);
               rv = savestrbuf(to.s, to.l);
               free(to.s);
            }
            goto jleave;
         default:
            /* Not our desired parameter, skip and continue */
            break;
         }
      }

jskip1st:
      if (*(p = _mime_param_skip(p)) == '\0')
         goto jleave;
   }

jleave:
   NYD_LEAVE;
   return rv;
}

FL char *
mime_param_boundary_get(char const *headerbody, size_t *len)
{
   char *q = NULL, *p;
   NYD_ENTER;

   if ((p = mime_param_get("boundary", headerbody)) != NULL) {
      size_t sz = strlen(p);

      if (len != NULL)
         *len = sz + 2;
      q = salloc(sz + 2 +1);
      q[0] = q[1] = '-';
      memcpy(q + 2, p, sz);
      *(q + sz + 2) = '\0';
   }
   NYD_LEAVE;
   return q;
}

FL char *
mime_param_boundary_create(void)
{
   char *bp;
   NYD_ENTER;

   bp = salloc(36 + 6 +1);
   bp[0] = bp[2] = bp[39] = bp[41] = '=';
   bp[1] = bp[40] = '-';
   memcpy(bp + 3, getrandstring(36), 36);
   bp[42] = '\0';
   NYD_LEAVE;
   return bp;
}

/* s-it-mode */
