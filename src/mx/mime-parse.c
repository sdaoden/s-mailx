/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of mime-parse.h. TODO
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
#define su_FILE mime_parse
#define mx_SOURCE
#define mx_SOURCE_MIME_PARSE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/cmd-charsetalias.h"
#include "mx/file-streams.h"
#include "mx/mime-enc.h"
#include "mx/mime-param.h"
#include "mx/mime-type.h"

#include "mx/mime-parse.h"
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

/* Fetch plain */
static char *a_mps_ct_plain_from_ct(char const *cth);

static boole a_mps_part(struct message *zmp, struct mimepart *ip,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf, int level);

static void a_mps_rfc822(struct message *zmp, struct mimepart *ip,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf, int level);

#ifdef mx_HAVE_TLS
static void a_mps_pkcs7(struct message *zmp, struct mimepart *ip,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf, int level);
#endif

static boole a_mps_multipart(struct message *zmp, struct mimepart *ip,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf, int level);
static void a__mps_new(struct mimepart *ip, struct mimepart **np, off_t offs,
      int *part);
static void a__mps_end(struct mimepart **np, off_t xoffs, long lines);

static char *
a_mps_ct_plain_from_ct(char const *cth){
   char *rv_b, *rv;
   NYD2_IN;

   rv_b = savestr(cth);

   if((rv = su_cs_find_c(rv_b, ';')) != NIL)
      *rv = '\0';

   rv = &rv_b[su_cs_len(rv_b)];
   while(rv > rv_b && su_cs_is_blank(rv[-1]))
      --rv;
   *rv = '\0';

   NYD2_OU;
   return rv_b;
}

static boole
a_mps_part(struct message *zmp, struct mimepart *ip,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf, int level){
   char const *cp;
   boole rv;
   NYD_IN;

   rv = FAL0;

   ip->m_ct_type = hfield1("content-type", R(struct message*,ip));
   if(ip->m_ct_type != NIL)
      ip->m_ct_type_plain = a_mps_ct_plain_from_ct(ip->m_ct_type);
   else if(ip->m_parent != NIL &&
         ip->m_parent->m_mime_type == mx_MIME_TYPE_DIGEST)
      ip->m_ct_type_plain = "message/rfc822";
   else
      ip->m_ct_type_plain = "text/plain";
   ip->m_ct_type_usr_ovwr = NIL;

   if((cp = ip->m_ct_type) != NIL)
      cp = mx_mime_param_get("charset", cp);
   if(cp == NIL)
      ip->m_charset = ok_vlook(charset_7bit);
   else
      ip->m_charset = mx_charsetalias_expand(cp, FAL0);

   if((ip->m_ct_enc = hfield1("content-transfer-encoding",
         R(struct message*,ip))) == NIL)
      ip->m_ct_enc = mx_mime_enc_name_from_conversion(CONV_7BIT);
   ip->m_mime_enc = mx_mime_enc_from_name(ip->m_ct_enc);

   if(((cp = hfield1("content-disposition", R(struct message*,ip))) == NIL ||
         (ip->m_filename = mx_mime_param_get("filename", cp)) == NIL) &&
         ip->m_ct_type != NIL)
      ip->m_filename = mx_mime_param_get("name", ip->m_ct_type);

   if((cp = hfield1("content-description", R(struct message*,ip))) != NIL)
      ip->m_content_description = cp;

   if((ip->m_mime_type = mx_mime_type_classify_part(ip,
         ((mpf & mx_MIME_PARSE_FOR_USER_CONTEXT) != 0))) == mx_MIME_TYPE_822){
      /* TODO (v15) HACK: message/rfc822 is treated special, that this one is
       * TODO too stupid to apply content-decoding when (falsely) applied */
      if(ip->m_mime_enc != mx_MIME_ENC_8B && ip->m_mime_enc != mx_MIME_ENC_7B){
         n_err(_("Pre-v15 %s cannot handle (falsely) encoded message/rfc822\n"
            "  (not 7bit or 8bit)!  Interpreting as text/plain!\n"),
            n_uagent);
         ip->m_mime_type = mx_MIME_TYPE_TEXT_PLAIN;
      }
   }

   ASSERT(ip->m_external_body_url == NIL);
   if(!su_cs_cmp_case(ip->m_ct_type_plain, "message/external-body") &&
         (cp = mx_mime_param_get("access-type", ip->m_ct_type)) != NIL &&
         !su_cs_cmp_case(cp, "URL"))
      ip->m_external_body_url = mx_mime_param_get("url", ip->m_ct_type);

   if(mpf & mx_MIME_PARSE_PARTS){
      if(level > 9999){ /* TODO MAGIC */
         n_err(_("MIME content too deeply nested\n"));
         goto jleave;
      }

      switch(ip->m_mime_type){
      case mx_MIME_TYPE_PKCS7:
         if(mpf & mx_MIME_PARSE_DECRYPT){
#ifdef mx_HAVE_TLS
            a_mps_pkcs7(zmp, ip, mpf, level);
            if(ip->m_content_info & CI_ENCRYPTED_OK)
               ip->m_content_info |= CI_EXPANDED;
            break;
#else
            n_err(_("No SSL / S/MIME support compiled in\n"));
            goto jleave;
#endif
         }
         break;
      default:
         break;
      case mx_MIME_TYPE_ALTERNATIVE:
      case mx_MIME_TYPE_RELATED: /* TODO /related handled EQ /alternative */
      case mx_MIME_TYPE_DIGEST:
      case mx_MIME_TYPE_SIGNED:
      case mx_MIME_TYPE_ENCRYPTED:
      case mx_MIME_TYPE_MULTI:
         if(!a_mps_multipart(zmp, ip, mpf, level))
            goto jleave;
         break;
      case mx_MIME_TYPE_822:
         a_mps_rfc822(zmp, ip, mpf, level);
         break;
      }
   }

   rv = TRU1;
jleave:
   NYD_OU;
   return rv;
}

static void
a_mps_rfc822(struct message *zmp, struct mimepart *ip,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf, int level){
   off_t offs;
   struct mimepart *np;
   long lines;
   uz cnt;
   FILE *ibuf;
   int lastc, c;
   NYD_IN;

   lastc = '\n';

   if((ibuf = setinput(&mb, R(struct message*,ip), NEED_BODY)) == NIL)
      goto jleave;

   cnt = ip->m_size;
   lines = ip->m_lines;
   while(cnt && ((c = getc(ibuf)) != EOF)){
      --cnt;
      if(c == '\n'){
         --lines;
         if(lastc == '\n')
            break;
      }
      lastc = c;
   }
   offs = ftell(ibuf);

   np = su_AUTO_CALLOC(sizeof *np);
   np->m_flag = MNOFROM;
   np->m_content_info = CI_HAVE_HEADER | CI_HAVE_BODY;
   np->m_block = mailx_blockof(offs);
   np->m_offset = mailx_offsetof(offs);
   np->m_size = np->m_xsize = cnt;
   np->m_lines = np->m_xlines = lines;
   /* C99 */{
      uz i;

      i = (ip->m_partstring != NIL) ? su_cs_len(ip->m_partstring) : 0;
      i += 20;
      np->m_partstring = su_AUTO_ALLOC(i);
      if(ip->m_partstring != NIL)
         snprintf(np->m_partstring, i, "%s.%d", ip->m_partstring, 1);
      else{
         np->m_partstring[0] = '1';
         np->m_partstring[1] = '\0';
      }
   }
   np->m_parent = ip;
   ip->m_multipart = np;

   if(!(mpf & mx_MIME_PARSE_SHALLOW) && ok_blook(rfc822_body_from_)){
      substdate(R(struct message*,np));
      np->m_from = fakefrom(R(struct message*,np));/* TODO strip MNOFROM? */
   }

   a_mps_part(zmp, np, mpf, level + 1);

jleave:
   NYD_OU;
}

#ifdef mx_HAVE_TLS
static void
a_mps_pkcs7(struct message *zmp, struct mimepart *ip,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf, int level){
   struct mimepart *np;
   struct message m, *xmp;
   char *to, *cc;
   NYD_IN;

   su_mem_copy(&m, ip, sizeof m);
   to = hfield1("to", zmp);
   cc = hfield1("cc", zmp);

   if((xmp = smime_decrypt(&m, to, cc, FAL0)) != NIL){
      np = su_AUTO_CALLOC(sizeof *np);
      np->m_flag = xmp->m_flag;
      np->m_content_info = xmp->m_content_info | CI_ENCRYPTED |
            CI_ENCRYPTED_OK;
      np->m_block = xmp->m_block;
      np->m_offset = xmp->m_offset;
      np->m_size = xmp->m_size;
      np->m_xsize = xmp->m_xsize;
      np->m_lines = xmp->m_lines;
      np->m_xlines = xmp->m_xlines;

      /* TODO using part "1" for decrypted content is a hack */
      if((np->m_partstring = ip->m_partstring) == NIL)
         ip->m_partstring = np->m_partstring = UNCONST(char*,n_1);

      if(a_mps_part(zmp, np, mpf, level + 1) == OKAY){
         ip->m_content_info |= CI_ENCRYPTED | CI_ENCRYPTED_OK;
         np->m_parent = ip;
         ip->m_multipart = np;
      }
   }else
      ip->m_content_info |= CI_ENCRYPTED | CI_ENCRYPTED_BAD;

   NYD_OU;
}
#endif /* mx_HAVE_TLS */

static boole
a_mps_multipart(struct message *zmp, struct mimepart *ip,
      BITENUM_IS(u32,mx_mime_parse_flags) mpf, int level){
   struct mimepart *np;
   int part;
   long lines;
   off_t offs;
   FILE *ibuf;
   uz linesize, linelen, boundlen, cnt;
   char *line, *boundary;
   boole rv;
   NYD_IN;

   rv = FAL0;
   mx_fs_linepool_aquire(&line, &linesize);

   if((boundary = mx_mime_param_boundary_get(ip->m_ct_type, &linelen)) == NIL)
      goto jleave;

   boundlen = linelen;
   if((ibuf = setinput(&mb, R(struct message*,ip), NEED_BODY)) == NIL)
      goto jleave;

   cnt = ip->m_size;
   for(;;){
      if(fgetline(&line, &linesize, &cnt, &linelen, ibuf, FAL0) == NIL){
         if(ferror(ibuf)) /* XXX */
            goto jleave;
         break;
      }
      if(line[0] == '\n')
         break;
   }
   offs = ftell(ibuf);

   /* TODO using part "1" for decrypted content is a hack */
   if(ip->m_partstring == NIL)
      ip->m_partstring = UNCONST(char*,n_1);
   a__mps_new(ip, &np, offs, NIL);

   lines = 0;
   part = 0;

   for(;;){
      if(fgetline(&line, &linesize, &cnt, &linelen, ibuf, FAL0) == NIL){
         if(ferror(ibuf))
            goto jleave;
         break;
      }

      /* XXX linelen includes LF */
      if(!((lines > 0 || part == 0) && linelen > boundlen &&
            !strncmp(line, boundary, boundlen))){
         ++lines;
         continue;
      }

      /* Subpart boundary? */
      if(line[boundlen] == '\n'){
         offs = ftell(ibuf);
         if(part > 0){
            a__mps_end(&np, offs - boundlen - 2, lines);
            a__mps_new(ip, &np, offs - boundlen - 2, NIL);
         }
         a__mps_end(&np, offs, 2);
         a__mps_new(ip, &np, offs, &part);
         lines = 0;
         continue;
      }

      /* Final boundary?  Be aware of cases where there is no separating
       * newline in between boundaries, as has been seen in a message with
       * "Content-Type: multipart/appledouble;" */
      if(linelen < boundlen + 2)
         continue;
      linelen -= boundlen + 2;
      if(line[boundlen] != '-' || line[boundlen + 1] != '-' ||
            (linelen > 0 && line[boundlen + 2] != '\n'))
         continue;
      offs = ftell(ibuf);
      if(part != 0){
         a__mps_end(&np, offs - boundlen - 4, lines);
         a__mps_new(ip, &np, offs - boundlen - 4, NIL);
      }
      a__mps_end(&np, offs + cnt, 2);
      break;
   }
   if(np){
      offs = ftell(ibuf);
      a__mps_end(&np, offs, lines);
   }

   for(np = ip->m_multipart; np != NIL; np = np->m_nextpart)
      if(np->m_mime_type != mx_MIME_TYPE_DISCARD)
         a_mps_part(zmp, np, mpf, level + 1);

   rv = TRU1;
jleave:
   mx_fs_linepool_release(line, linesize);

   NYD_OU;
   return rv;
}

static void
a__mps_new(struct mimepart *ip, struct mimepart **np, off_t offs, int *part){
   struct mimepart *pp;
   NYD2_IN;

   *np = su_AUTO_CALLOC(sizeof **np);
   (*np)->m_flag = MNOFROM;
   (*np)->m_content_info = CI_HAVE_HEADER | CI_HAVE_BODY;
   (*np)->m_block = mailx_blockof(offs);
   (*np)->m_offset = mailx_offsetof(offs);

   if(part != NIL){
      uz i;

      ++(*part);
      i = (ip->m_partstring != NIL) ? su_cs_len(ip->m_partstring) : 0;
      i += 20;
      (*np)->m_partstring = su_AUTO_ALLOC(i);
      if(ip->m_partstring != NIL)
         snprintf((*np)->m_partstring, i, "%s.%d", ip->m_partstring, *part);
      else
         snprintf((*np)->m_partstring, i, "%d", *part);
   }else
      (*np)->m_mime_type = mx_MIME_TYPE_DISCARD;

   (*np)->m_parent = ip;

   if(ip->m_multipart){
      for(pp = ip->m_multipart; pp->m_nextpart != NIL; pp = pp->m_nextpart)
         ;
      pp->m_nextpart = *np;
   }else
      ip->m_multipart = *np;

   NYD2_OU;
}

static void
a__mps_end(struct mimepart **np, off_t xoffs, long lines){
   off_t offs;
   NYD_IN;

   offs = mailx_positionof((*np)->m_block, (*np)->m_offset);
   (*np)->m_size = (*np)->m_xsize = xoffs - offs;
   (*np)->m_lines = (*np)->m_xlines = lines;
   *np = NIL;

   NYD_OU;
}

struct mimepart *
mx_mime_parse_msg(struct message *mp, BITENUM_IS(u32,mx_mime_parse_flags) mpf){
   struct mimepart *ip;
   NYD_IN;

   ip = su_AUTO_CALLOC(sizeof *ip);
   ip->m_flag = mp->m_flag;
   ip->m_content_info = mp->m_content_info;
   ip->m_block = mp->m_block;
   ip->m_offset = mp->m_offset;
   ip->m_size = mp->m_size;
   ip->m_xsize = mp->m_xsize;
   ip->m_lines = mp->m_lines;
   ip->m_xlines = mp->m_lines;

   if(!a_mps_part(mp, ip, mpf, 0))
      ip = NIL;

   NYD_OU;
   return ip;
}

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_MIME_PARSE
/* s-it-mode */
