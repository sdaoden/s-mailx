/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Parse a message into a tree of struct mimepart objects.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
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
#define n_FILE mime_parse

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

static bool_t  _mime_parse_part(struct message *zmp, struct mimepart *ip,
                  enum mime_parse_flags mpf, int level);

static void    _mime_parse_rfc822(struct message *zmp, struct mimepart *ip,
                  enum mime_parse_flags mpf, int level);

#ifdef HAVE_SSL
static void    _mime_parse_pkcs7(struct message *zmp, struct mimepart *ip,
                  enum mime_parse_flags mpf, int level);
#endif

static void    _mime_parse_multipart(struct message *zmp,
                  struct mimepart *ip, enum mime_parse_flags mpf, int level);
static void    __mime_parse_new(struct mimepart *ip, struct mimepart **np,
                  off_t offs, int *part);
static void    __mime_parse_end(struct mimepart **np, off_t xoffs,
                  long lines);

static bool_t
_mime_parse_part(struct message *zmp, struct mimepart *ip,
   enum mime_parse_flags mpf, int level)
{
   char *cp_b, *cp;
   bool_t rv = FAL0;
   NYD_ENTER;

   ip->m_ct_type = hfield1("content-type", (struct message*)ip);
   if (ip->m_ct_type != NULL) {
      ip->m_ct_type_plain = cp_b = savestr(ip->m_ct_type);
      if ((cp = strchr(cp_b, ';')) != NULL)
         *cp = '\0';
      cp = cp_b + strlen(cp_b);
      while (cp > cp_b && blankchar(cp[-1]))
         --cp;
      *cp = '\0';
   } else if (ip->m_parent != NULL &&
         ip->m_parent->m_mimecontent == MIME_DIGEST)
      ip->m_ct_type_plain = "message/rfc822";
   else
      ip->m_ct_type_plain = "text/plain";
   ip->m_ct_type_usr_ovwr = NULL;

   if (ip->m_ct_type != NULL)
      ip->m_charset = mime_param_get("charset", ip->m_ct_type);
   if (ip->m_charset == NULL)
      ip->m_charset = charset_get_7bit();

   if ((ip->m_ct_enc = hfield1("content-transfer-encoding",
         (struct message*)ip)) == NULL)
      ip->m_ct_enc = mime_enc_from_conversion(CONV_7BIT);
   ip->m_mime_enc = mime_enc_from_ctehead(ip->m_ct_enc);

   if (((cp = hfield1("content-disposition", (struct message*)ip)) == NULL ||
         (ip->m_filename = mime_param_get("filename", cp)) == NULL) &&
         ip->m_ct_type != NULL)
      ip->m_filename = mime_param_get("name", ip->m_ct_type);

   ip->m_mimecontent = mime_type_mimepart_content(ip);

   if (mpf & MIME_PARSE_PARTS) {
      if (level > 9999) { /* TODO MAGIC */
         n_err(_("MIME content too deeply nested\n"));
         goto jleave;
      }
      switch (ip->m_mimecontent) {
      case MIME_PKCS7:
         if (mpf & MIME_PARSE_DECRYPT) {
#ifdef HAVE_SSL
            _mime_parse_pkcs7(zmp, ip, mpf, level);
            break;
#else
            n_err(_("No SSL support compiled in\n"));
            goto jleave;
#endif
         }
         /* FALLTHRU */
      default:
         break;
      case MIME_MULTI:
      case MIME_ALTERNATIVE:
      case MIME_RELATED: /* TODO /related yet handled like /alternative */
      case MIME_DIGEST:
         _mime_parse_multipart(zmp, ip, mpf, level);
         break;
      case MIME_822:
         _mime_parse_rfc822(zmp, ip, mpf, level);
         break;
      }
   }

   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

static void
_mime_parse_rfc822(struct message *zmp, struct mimepart *ip,
   enum mime_parse_flags mpf, int level)
{
   int c, lastc = '\n';
   size_t cnt;
   FILE *ibuf;
   off_t offs;
   struct mimepart *np;
   long lines;
   NYD_ENTER;

   if ((ibuf = setinput(&mb, (struct message*)ip, NEED_BODY)) == NULL)
      goto jleave;

   cnt = ip->m_size;
   lines = ip->m_lines;
   while (cnt && ((c = getc(ibuf)) != EOF)) {
      --cnt;
      if (c == '\n') {
         --lines;
         if (lastc == '\n')
            break;
      }
      lastc = c;
   }
   offs = ftell(ibuf);

   np = csalloc(1, sizeof *np);
   np->m_flag = MNOFROM;
   np->m_have = HAVE_HEADER | HAVE_BODY;
   np->m_block = mailx_blockof(offs);
   np->m_offset = mailx_offsetof(offs);
   np->m_size = np->m_xsize = cnt;
   np->m_lines = np->m_xlines = lines;
   np->m_partstring = ip->m_partstring;
   np->m_parent = ip;
   ip->m_multipart = np;

   if (ok_blook(rfc822_body_from_)) {
      substdate((struct message*)np);
      np->m_from = fakefrom((struct message*)np);/* TODO strip MNOFROM flag? */
   }

   _mime_parse_part(zmp, np, mpf, level + 1);
jleave:
   NYD_LEAVE;
}

#ifdef HAVE_SSL
static void
_mime_parse_pkcs7(struct message *zmp, struct mimepart *ip,
   enum mime_parse_flags mpf, int level)
{
   struct message m, *xmp;
   struct mimepart *np;
   char *to, *cc;
   NYD_ENTER;

   memcpy(&m, ip, sizeof m);
   to = hfield1("to", zmp);
   cc = hfield1("cc", zmp);

   if ((xmp = smime_decrypt(&m, to, cc, 0)) != NULL) {
      np = csalloc(1, sizeof *np);
      np->m_flag = xmp->m_flag;
      np->m_have = xmp->m_have;
      np->m_block = xmp->m_block;
      np->m_offset = xmp->m_offset;
      np->m_size = xmp->m_size;
      np->m_xsize = xmp->m_xsize;
      np->m_lines = xmp->m_lines;
      np->m_xlines = xmp->m_xlines;
      np->m_partstring = ip->m_partstring;

      if (_mime_parse_part(zmp, np, mpf, level + 1) == OKAY) {
         np->m_parent = ip;
         ip->m_multipart = np;
      }
   }
   NYD_LEAVE;
}
#endif /* HAVE_SSL */

static void
_mime_parse_multipart(struct message *zmp, struct mimepart *ip,
   enum mime_parse_flags mpf, int level)
{
   /* TODO Instead of the recursive multiple run parse we have today,
    * TODO the send/MIME layer rewrite must create a "tree" of parts with
    * TODO a single-pass parse, then address each part directly as
    * TODO necessary; since boundaries start with -- and the content
    * TODO rather forms a stack this is pretty cheap indeed! */
   struct mimepart *np = NULL;
   char *boundary, *line = NULL;
   size_t linesize = 0, linelen, cnt, boundlen;
   FILE *ibuf;
   off_t offs;
   int part = 0;
   long lines = 0;
   NYD_ENTER;

   if ((boundary = mime_param_boundary_get(ip->m_ct_type, &linelen)) == NULL)
      goto jleave;

   boundlen = linelen;
   if ((ibuf = setinput(&mb, (struct message*)ip, NEED_BODY)) == NULL)
      goto jleave;

   cnt = ip->m_size;
   while (fgetline(&line, &linesize, &cnt, &linelen, ibuf, 0))
      if (line[0] == '\n')
         break;
   offs = ftell(ibuf);

   __mime_parse_new(ip, &np, offs, NULL);
   while (fgetline(&line, &linesize, &cnt, &linelen, ibuf, 0)) {
      /* XXX linelen includes LF */
      if (!((lines > 0 || part == 0) && linelen > boundlen &&
            !strncmp(line, boundary, boundlen))) {
         ++lines;
         continue;
      }

      /* Subpart boundary? */
      if (line[boundlen] == '\n') {
         offs = ftell(ibuf);
         if (part > 0) {
            __mime_parse_end(&np, offs - boundlen - 2, lines);
            __mime_parse_new(ip, &np, offs - boundlen - 2, NULL);
         }
         __mime_parse_end(&np, offs, 2);
         __mime_parse_new(ip, &np, offs, &part);
         lines = 0;
         continue;
      }

      /* Final boundary?  Be aware of cases where there is no separating
       * newline in between boundaries, as has been seen in a message with
       * "Content-Type: multipart/appledouble;" */
      if (linelen < boundlen + 2)
         continue;
      linelen -= boundlen + 2;
      if (line[boundlen] != '-' || line[boundlen + 1] != '-' ||
            (linelen > 0 && line[boundlen + 2] != '\n'))
         continue;
      offs = ftell(ibuf);
      if (part != 0) {
         __mime_parse_end(&np, offs - boundlen - 4, lines);
         __mime_parse_new(ip, &np, offs - boundlen - 4, NULL);
      }
      __mime_parse_end(&np, offs + cnt, 2);
      break;
   }
   if (np) {
      offs = ftell(ibuf);
      __mime_parse_end(&np, offs, lines);
   }

   for (np = ip->m_multipart; np != NULL; np = np->m_nextpart)
      if (np->m_mimecontent != MIME_DISCARD)
         _mime_parse_part(zmp, np, mpf, level + 1);
   free(line);
jleave:
   NYD_LEAVE;
}

static void
__mime_parse_new(struct mimepart *ip, struct mimepart **np, off_t offs,
   int *part)
{
   struct mimepart *pp;
   size_t sz;
   NYD_ENTER;

   *np = csalloc(1, sizeof **np);
   (*np)->m_flag = MNOFROM;
   (*np)->m_have = HAVE_HEADER | HAVE_BODY;
   (*np)->m_block = mailx_blockof(offs);
   (*np)->m_offset = mailx_offsetof(offs);

   if (part) {
      ++(*part);
      sz = (ip->m_partstring != NULL) ? strlen(ip->m_partstring) : 0;
      sz += 20;
      (*np)->m_partstring = salloc(sz);
      if (ip->m_partstring)
         snprintf((*np)->m_partstring, sz, "%s.%u", ip->m_partstring, *part);
      else
         snprintf((*np)->m_partstring, sz, "%u", *part);
   } else
      (*np)->m_mimecontent = MIME_DISCARD;
   (*np)->m_parent = ip;

   if (ip->m_multipart) {
      for (pp = ip->m_multipart; pp->m_nextpart != NULL; pp = pp->m_nextpart)
         ;
      pp->m_nextpart = *np;
   } else
      ip->m_multipart = *np;
   NYD_LEAVE;
}

static void
__mime_parse_end(struct mimepart **np, off_t xoffs, long lines)
{
   off_t offs;
   NYD_ENTER;

   offs = mailx_positionof((*np)->m_block, (*np)->m_offset);
   (*np)->m_size = (*np)->m_xsize = xoffs - offs;
   (*np)->m_lines = (*np)->m_xlines = lines;
   *np = NULL;
   NYD_LEAVE;
}

FL struct mimepart *
mime_parse_msg(struct message *mp, enum mime_parse_flags mpf)
{
   struct mimepart *ip;
   NYD_ENTER;

   ip = csalloc(1, sizeof *ip);
   ip->m_flag = mp->m_flag;
   ip->m_have = mp->m_have;
   ip->m_block = mp->m_block;
   ip->m_offset = mp->m_offset;
   ip->m_size = mp->m_size;
   ip->m_xsize = mp->m_xsize;
   ip->m_lines = mp->m_lines;
   ip->m_xlines = mp->m_lines;
   if (!_mime_parse_part(mp, ip, mpf, 0))
      ip = NULL;
   NYD_LEAVE;
   return ip;
}

/* s-it-mode */
