/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Iterating over, and over such housekeeping message user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef su_FILE
#define su_FILE cmd_msg
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/icodec.h>

#include "mx/cmd.h"
#include "mx/colour.h"
#include "mx/file-streams.h"
#include "mx/ignore.h"
#include "mx/termios.h"

/* TODO fake */
#include "su/code-in.h"

/* Prepare and print "[Message: xy]:" intro */
static boole a_cmsg_show_overview(FILE *obuf, struct message *mp, int msg_no);

/* Show the requested messages */
static int     _type1(int *msgvec, boole doign, boole dopage, boole dopipe,
                  boole donotdecode, char *cmd, u64 *tstats);

/* Pipe the requested messages */
static int a_cmsg_pipe1(void *vp, boole doign);

/* `top' / `Top' */
static int a_cmsg_top(void *vp, struct mx_ignore const *itp);

/* Delete the indicated messages.  Set dot to some nice place afterwards */
static int     delm(int *msgvec);

static boole
a_cmsg_show_overview(FILE *obuf, struct message *mp, int msg_no){
   boole rv;
   char const *cpre, *csuf;
   NYD2_IN;

   cpre = csuf = n_empty;
#ifdef mx_HAVE_COLOUR
   if(mx_COLOUR_IS_ACTIVE()){
      struct mx_colour_pen *cpen;

      if((cpen = mx_colour_pen_create(mx_COLOUR_ID_VIEW_MSGINFO, NULL)
            ) != NIL){
         struct str const *s;

         if((s = mx_colour_pen_to_str(cpen)) != NIL)
            cpre = s->s;
         if((s = mx_colour_reset_to_str()) != NIL)
            csuf = s->s;
      }
   }
#endif
   /* XXX Message info uses wire format for line count */
   rv = (fprintf(obuf,
         A_("%s[-- Message %2d -- %lu lines, %lu bytes --]:%s\n"),
         cpre, msg_no, (ul)mp->m_lines, (ul)mp->m_size, csuf) > 0);
   NYD2_OU;
   return rv;
}

static int
_type1(int *msgvec, boole doign, boole dopage, boole dopipe,
   boole donotdecode, char *cmd, u64 *tstats)
{
   u64 mstats[1];
   int *ip;
   struct message *mp;
   char const *cp;
   enum sendaction action;
   boole volatile formfeed;
   FILE * volatile obuf;
   int volatile rv;
   NYD_IN;

   rv = 1;
   obuf = n_stdout;
   formfeed = (dopipe && ok_blook(page));
   action = ((dopipe && ok_blook(piperaw))
         ? SEND_MBOX : donotdecode
         ? SEND_SHOW : doign
         ? SEND_TODISP : SEND_TODISP_ALL);
   UNINIT(cp, NIL);

   if(dopipe){
      if((obuf = mx_fs_pipe_open(cmd, "w", ok_vlook(SHELL), NIL, -1)) == NIL){
         n_perr(cmd, 0);
         obuf = n_stdout;
      }
   } else if ((n_psonce & n_PSO_TTYOUT) && (dopage ||
         ((n_psonce & n_PSO_INTERACTIVE) && (cp = ok_vlook(crt)) != NULL))) {
      uz nlines, lib;

      nlines = 0;

      if (!dopage) {
         for (ip = msgvec; *ip && PCMP(ip - msgvec, <, msgCount); ++ip) {
            mp = message + *ip - 1;
            if (!(mp->m_content_info & CI_HAVE_BODY))
               if (get_body(mp) != OKAY)
                  goto jleave;
            nlines += mp->m_lines + 1; /* TODO BUT wire format, not display! */
         }
      }

      /* >= not <: we return to the prompt */
      if(dopage || nlines >= (*cp != '\0'
               ? (su_idec_uz_cp(&lib, cp, 0, NULL), lib)
               : S(uz,mx_termios_dimen.tiosd_real_height))){
         if((obuf = mx_pager_open()) == NULL)
            obuf = n_stdout;
      }
      mx_COLOUR(
         if(action == SEND_TODISP || action == SEND_TODISP_ALL)
            mx_colour_env_create(mx_COLOUR_CTX_VIEW, obuf, obuf != n_stdout);
      )
   }
   mx_COLOUR(
      else if(action == SEND_TODISP || action == SEND_TODISP_ALL)
         mx_colour_env_create(mx_COLOUR_CTX_VIEW, n_stdout, FAL0);
   )

   rv = 0;
   n_autorec_relax_create();
   for (ip = msgvec; *ip && PCMP(ip - msgvec, <, msgCount); ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      setdot(mp);
      n_pstate |= n_PS_DID_PRINT_DOT;
      uncollapse1(mp, 1);
      if(!dopipe && ip != msgvec && fprintf(obuf, "\n") < 0){
         rv = 1;
         break;
      }
      if(action != SEND_MBOX && !a_cmsg_show_overview(obuf, mp, *ip)){
         rv = 1;
         break;
      }
      if(sendmp(mp, obuf, (doign ? mx_IGNORE_TYPE : NIL), NIL, action, mstats
            ) < 0){
         rv = 1;
         break;
      }
      n_autorec_relax_unroll();
      if(formfeed){ /* TODO a nicer way to separate piped messages! */
         if(putc('\f', obuf) == EOF){
            rv = 1;
            break;
         }
      }
      if (tstats != NULL)
         tstats[0] += mstats[0];
   }
   n_autorec_relax_gut();
   mx_COLOUR(
      if(!dopipe && (action == SEND_TODISP || action == SEND_TODISP_ALL))
         mx_colour_env_gut();
   )

jleave:
   if(obuf != n_stdout)
      mx_pager_close(obuf);
   else
      clearerr(obuf);

   NYD_OU;
   return rv;
}

static int
a_cmsg_pipe1(void *vp, boole doign){
   u64 stats[1];
   char const *cmd, *cmdq;
   int *msgvec, rv;
   struct mx_cmd_arg *cap;
   struct mx_cmd_arg_ctx *cacp;
   NYD2_IN;

   cacp = vp;
   cap = cacp->cac_arg;
   msgvec = cap->ca_arg.ca_msglist;
   cap = cap->ca_next;
   rv = 1;

   if((cmd = cap->ca_arg.ca_str.s)[0] == '\0' &&
         ((cmd = ok_vlook(cmd)) == NULL || *cmd == '\0')){
      n_err(_("%s: variable *cmd* not set\n"), cacp->cac_desc->cad_name);
      goto jleave;
   }

   cmdq = n_shexp_quote_cp(cmd, FAL0);
   fprintf(n_stdout, _("Pipe to: %s\n"), cmdq);
   stats[0] = 0;
   if((rv = _type1(msgvec, doign, FAL0, TRU1, FAL0, n_UNCONST(cmd), stats)
         ) == 0)
      fprintf(n_stdout, "%s %" PRIu64 " bytes\n", cmdq, stats[0]);
jleave:
   NYD2_OU;
   return rv;
}

static int
a_cmsg_top(void *vp, struct mx_ignore const *itp){
   struct n_string s;
   int *msgvec, *ip;
   enum{a_NONE, a_SQUEEZE = 1u<<0,
      a_EMPTY = 1u<<8, a_STOP = 1u<<9,  a_WORKMASK = 0xFF00u} f;
   uz tmax, plines;
   FILE *iobuf, *pbuf;
   NYD2_IN;

   if((iobuf = mx_fs_tmp_open("topio", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL){
      n_perr(_("top: I/O temporary file"), 0);
      vp = NIL;
      goto jleave;
   }
   if((pbuf = mx_fs_tmp_open("toppag", (mx_FS_O_RDWR | mx_FS_O_UNLINK |
            mx_FS_O_REGISTER), NIL)) == NIL)
      pbuf = n_stdout;

   /* TODO In v15 we should query the m_message object, and directly send only
    * TODO those parts, optionally over empty-line-squeeze and quote-strip
    * TODO filters, in which we are interested in: only text content!
    * TODO And: with *topsqueeze*, header/content separating empty line.. */
   n_pstate &= ~n_PS_MSGLIST_DIRECT; /* TODO NO ATTACHMENTS */
   plines = 0;

   mx_COLOUR( mx_colour_env_create(mx_COLOUR_CTX_VIEW, iobuf, FAL0); )
   n_string_creat_auto(&s);
   /* C99 */{
      sz l;

      if((su_idec_sz_cp(&l, ok_vlook(toplines), 0, NULL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED)
         l = 0;
      if(l <= 0){
         tmax = n_screensize();
         if(l < 0){
            l = ABS(l);
            tmax >>= l;
         }
      }else
         tmax = (uz)l;
   }
   f = ok_blook(topsqueeze) ? a_SQUEEZE : a_NONE;

   for(ip = msgvec = vp; *ip != 0; ++ip){
      struct message *mp;

      mp = &message[*ip - 1];
      touch(mp);
      setdot(mp);
      n_pstate |= n_PS_DID_PRINT_DOT;
      uncollapse1(mp, 1);

      rewind(iobuf);
      if(ftruncate(fileno(iobuf), 0)){
         n_perr(_("top: ftruncate(2)"), 0);
         vp = NULL;
         break;
      }

      if(!a_cmsg_show_overview(iobuf, mp, *ip) ||
            sendmp(mp, iobuf, itp, NULL, SEND_TODISP_ALL, NULL) < 0){
         n_err(_("top: failed to prepare message %d\n"), *ip);
         vp = NULL;
         break;
      }
      fflush_rewind(iobuf);

      /* TODO Skip over the _msg_overview line -- this is a hack to make
       * TODO colours work: colour contexts should be objects */
      for(;;){
         int c;

         if((c = getc(iobuf)) == EOF || putc(c, pbuf) == EOF){
            vp = NULL;
            break;
         }else if(c == '\n')
            break;
      }
      if(vp == NULL)
         break;
      ++plines;

      /* C99 */{
         uz l;

         n_string_trunc(&s, 0);
         for(l = 0, f &= ~a_WORKMASK; !(f & a_STOP);){
            int c;

            if((c = getc(iobuf)) == EOF){
               f |= a_STOP;
               c = '\n';
            }

            if(c != '\n')
               n_string_push_c(&s, c);
            else if((f & a_SQUEEZE) && s.s_len == 0){
               if(!(f & a_STOP) && ((f & a_EMPTY) || tmax - 1 <= l))
                  continue;
               if(putc('\n', pbuf) == EOF){
                  vp = NULL;
                  break;
               }
               f |= a_EMPTY;
               ++l;
            }else{
               char const *cp, *xcp;

               cp = n_string_cp_const(&s);
               /* TODO Brute simple skip part overviews; see above.. */
               if(!(f & a_SQUEEZE))
                  c = '\1';
               else if(s.s_len > 8 &&
                     (xcp = su_cs_find(cp, "[-- ")) != NULL &&
                      su_cs_find(&xcp[1], " --]") != NULL)
                  c = '\0';
               else{
                  char const *qcp;

                  for(qcp = ok_vlook(quote_chars); (c = *cp) != '\0'; ++cp){
                     if(!su_cs_is_ascii(c))
                        break;
                     if(!su_cs_is_space(c)){
                        if(su_cs_find_c(qcp, c) == NULL)
                           break;
                        c = '\0';
                        break;
                     }
                  }
               }

               if(c != '\0'){
                  if(fputs(n_string_cp_const(&s), pbuf) == EOF ||
                        putc('\n', pbuf) == EOF){
                     vp = NULL;
                     break;
                  }
                  if(++l >= tmax)
                     break;
                  f &= ~a_EMPTY;
               }else
                  f |= a_EMPTY;
               n_string_trunc(&s, 0);
            }
         }
         if(vp == NULL)
            break;
         if(l > 0)
            plines += l;
         else{
            if(!(f & a_EMPTY) && putc('\n', pbuf) == EOF){
               vp = NULL;
               break;
            }
            ++plines;
         }
      }
   }

   n_string_gut(&s);
   mx_COLOUR( mx_colour_env_gut(); )

   if(pbuf != n_stdout){
      page_or_print(pbuf, plines);

      mx_fs_close(pbuf);
   }else
      clearerr(pbuf);

   mx_fs_close(iobuf);

jleave:
   NYD2_OU;
   return (vp != NIL);
}

static int
delm(int *msgvec)
{
   struct message *mp;
   int rv = -1, *ip, last;
   NYD_IN;

   last = 0;
   for (ip = msgvec; *ip != 0; ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      mp->m_flag |= MDELETED | MTOUCH;
      mp->m_flag &= ~(MPRESERVE | MSAVED | MBOX);
      last = *ip;
   }
   if (last != 0) {
      setdot(message + last - 1);
      last = first(0, MDELETED);
      if (last != 0) {
         setdot(message + last - 1);
         rv = 0;
      } else {
         setdot(message);
      }
   }
   NYD_OU;
   return rv;
}

FL int
c_more(void *v)
{
   int *msgvec = v, rv;
   NYD_IN;

   rv = _type1(msgvec, TRU1, TRU1, FAL0, FAL0, NULL, NULL);
   NYD_OU;
   return rv;
}

FL int
c_More(void *v)
{
   int *msgvec = v, rv;
   NYD_IN;

   rv = _type1(msgvec, FAL0, TRU1, FAL0, FAL0, NULL, NULL);
   NYD_OU;
   return rv;
}

FL int
c_type(void *v)
{
   int *msgvec = v, rv;
   NYD_IN;

   rv = _type1(msgvec, TRU1, FAL0, FAL0, FAL0, NULL, NULL);
   NYD_OU;
   return rv;
}

FL int
c_Type(void *v)
{
   int *msgvec = v, rv;
   NYD_IN;

   rv = _type1(msgvec, FAL0, FAL0, FAL0, FAL0, NULL, NULL);
   NYD_OU;
   return rv;
}

FL int
c_show(void *v)
{
   int *msgvec = v, rv;
   NYD_IN;

   rv = _type1(msgvec, FAL0, FAL0, FAL0, TRU1, NULL, NULL);
   NYD_OU;
   return rv;
}

FL int
c_mimeview(void *vp){ /* TODO direct addressable parts, multiple such */
   struct message *mp;
   int rv, *msgvec;
   NYD_IN;

   if((msgvec = vp)[1] != 0){
      n_err(_("mimeview: can yet only take one message, sorry!\n"));/* TODO */
      n_pstate_err_no = su_ERR_NOTSUP;
      rv = 1;
      goto jleave;
   }

   mp = &message[*msgvec - 1];
   touch(mp);
   setdot(mp);
   n_pstate |= n_PS_DID_PRINT_DOT;
   uncollapse1(mp, 1);

   mx_COLOUR( mx_colour_env_create(mx_COLOUR_CTX_VIEW, n_stdout, FAL0); )

   if(!a_cmsg_show_overview(n_stdout, mp, *msgvec))
      n_pstate_err_no = su_ERR_IO;
   else if(sendmp(mp, n_stdout, mx_IGNORE_TYPE, NIL, SEND_TODISP_PARTS,
         NIL) < 0)
      n_pstate_err_no = su_ERR_IO;
   else
      n_pstate_err_no = su_ERR_NONE;

   mx_COLOUR( mx_colour_env_gut(); )

   rv = (n_pstate_err_no != su_ERR_NONE);
jleave:
   NYD_OU;
   return rv;
}

FL int
c_pipe(void *vp){
   int rv;
   NYD_IN;

   rv = a_cmsg_pipe1(vp, TRU1);
   NYD_OU;
   return rv;
}

FL int
c_Pipe(void *vp){
   int rv;
   NYD_IN;

   rv = a_cmsg_pipe1(vp, FAL0);
   NYD_OU;
   return rv;
}

FL int
c_top(void *v){
   struct mx_ignore *itp;
   int rv;
   NYD_IN;

   if(mx_ignore_is_any(mx_IGNORE_TOP))
      itp = mx_IGNORE_TOP;
   else{
      itp = mx_ignore_new(TRU1);
      mx_ignore_insert(itp, TRU1, "from", sizeof("from") -1);
      mx_ignore_insert(itp, TRU1, "to", sizeof("to") -1);
      mx_ignore_insert(itp, TRU1, "cc", sizeof("cc") -1);
      mx_ignore_insert(itp, TRU1, "subject", sizeof("subject") -1);
   }

   rv = !a_cmsg_top(v, itp);
   NYD_OU;
   return rv;
}

FL int
c_Top(void *v){
   int rv;
   NYD_IN;

   rv = !a_cmsg_top(v, mx_IGNORE_TYPE);
   NYD_OU;
   return rv;
}

FL int
c_next(void *v)
{
   int list[2], *ip, *ip2, mdot, *msgvec = v, rv = 1;
   struct message *mp;
   NYD_IN;

   if (*msgvec != 0) {
      /* If some messages were supplied, find the first applicable one
       * following dot using wrap around */
      mdot = (int)P2UZ(dot - message + 1);

      /* Find first message in supplied message list which follows dot */
      for (ip = msgvec; *ip != 0; ++ip) {
         if ((mb.mb_threaded ? message[*ip - 1].m_threadpos > dot->m_threadpos
               : *ip > mdot))
            break;
      }
      if (*ip == 0)
         ip = msgvec;
      ip2 = ip;
      do {
         mp = message + *ip2 - 1;
         if (!(mp->m_flag & MMNDEL)) {
            setdot(mp);
            goto jhitit;
         }
         if (*ip2 != 0)
            ++ip2;
         if (*ip2 == 0)
            ip2 = msgvec;
      } while (ip2 != ip);
      fprintf(n_stdout, _("No messages applicable\n"));
      goto jleave;
   }

   /* If this is the first command, select message 1.  Note that this must
    * exist for us to get here at all */
   if (!(n_pstate & n_PS_SAW_COMMAND)) {
      if (msgCount == 0)
         goto jateof;
      goto jhitit;
   }

   /* Just find the next good message after dot, no wraparound */
   if (mb.mb_threaded == 0) {
      for (mp = dot + !!(n_pstate & n_PS_DID_PRINT_DOT);
            PCMP(mp, <, message + msgCount); ++mp)
         if (!(mp->m_flag & MMNORM))
            break;
   } else {
      /* TODO The threading code had some bugs that caused crashes.
       * TODO The last thing (before the deep look) happens here,
       * TODO let's not trust n_PS_DID_PRINT_DOT but check & hope it fixes */
      if ((mp = dot) != NULL && (n_pstate & n_PS_DID_PRINT_DOT))
         mp = next_in_thread(mp);
      while (mp != NULL && (mp->m_flag & MMNORM))
         mp = next_in_thread(mp);
   }
   if (mp == NULL || PCMP(mp, >=, message + msgCount)) {
jateof:
      fprintf(n_stdout, _("At EOF\n"));
      rv = 0;
      goto jleave;
   }
   setdot(mp);

   /* Print dot */
jhitit:
   list[0] = (int)P2UZ(dot - message + 1);
   list[1] = 0;
   rv = c_type(list);
jleave:
   NYD_OU;
   return rv;
}

FL int
c_pdot(void *vp){
   char cbuf[su_IENC_BUFFER_SIZE], sep1, sep2;
   struct n_string s_b, *s;
   int *mlp;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;
   UNUSED(vp);

   n_pstate_err_no = su_ERR_NONE;
   s = n_string_creat_auto(&s_b);
   sep1 = *ok_vlook(ifs);
   sep2 = *ok_vlook(ifs_ws);
   if(sep1 == sep2)
      sep2 = '\0';
   if(sep1 == '\0')
      sep1 = ' ';

   cacp = vp;

   for(mlp = cacp->cac_arg->ca_arg.ca_msglist; *mlp != 0; ++mlp){
      if(!n_string_can_book(s, su_IENC_BUFFER_SIZE + 2u)){
         n_err(_("=: overflow: string too long!\n"));
         n_pstate_err_no = su_ERR_OVERFLOW;
         vp = NULL;
         goto jleave;
      }
      if(s->s_len > 0){
         s = n_string_push_c(s, sep1);
         if(sep2 != '\0')
            s = n_string_push_c(s, sep2);
      }
      s = n_string_push_cp(s,
            su_ienc(cbuf, (u32)*mlp, 10, su_IENC_MODE_NONE));
   }

   (void)n_string_cp(s);
   if(cacp->cac_vput == NULL){
      if(fprintf(n_stdout, "%s\n", s->s_dat) < 0){
         n_pstate_err_no = su_err_no();
         vp = NULL;
      }
   }else if(!n_var_vset(cacp->cac_vput, (up)s->s_dat)){
      n_pstate_err_no = su_ERR_NOTSUP;
      vp = NULL;
   }
jleave:
   /* n_string_gut(s); */
   NYD_OU;
   return (vp == NULL);
}

FL int
c_messize(void *v)
{
   int *msgvec = v, *ip, mesg;
   struct message *mp;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      mesg = *ip;
      mp = message + mesg - 1;
      fprintf(n_stdout, "%d: ", mesg);
      if (mp->m_xlines > 0)
         fprintf(n_stdout, "%ld", mp->m_xlines);
      else
         putc(' ', n_stdout);
      fprintf(n_stdout, "/%lu\n", (ul)mp->m_xsize);
   }
   NYD_OU;
   return 0;
}

FL int
c_delete(void *v)
{
   int *msgvec = v;
   NYD_IN;

   delm(msgvec);
   NYD_OU;
   return 0;
}

FL int
c_deltype(void *v)
{
   int list[2], rv = 0, *msgvec = v, lastdot;
   NYD_IN;

   lastdot = (int)P2UZ(dot - message + 1);
   if (delm(msgvec) >= 0) {
      list[0] = (int)P2UZ(dot - message + 1);
      if (list[0] > lastdot) {
         touch(dot);
         list[1] = 0;
         rv = c_type(list);
         goto jleave;
      }
      fprintf(n_stdout, _("At EOF\n"));
   } else
      fprintf(n_stdout, _("No more messages\n"));
jleave:
   NYD_OU;
   return rv;
}

FL int
c_undelete(void *v)
{
   int *msgvec = v, *ip;
   struct message *mp;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      mp = &message[*ip - 1];
      touch(mp);
      setdot(mp);
      if (mp->m_flag & (MDELETED | MSAVED))
         mp->m_flag &= ~(MDELETED | MSAVED);
      else
         mp->m_flag &= ~MDELETED;
#ifdef mx_HAVE_IMAP
      if (mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE)
         imap_undelete(mp, *ip);
#endif
   }
   NYD_OU;
   return 0;
}

FL int
c_stouch(void *v)
{
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      setdot(message + *ip - 1);
      dot->m_flag |= MTOUCH;
      dot->m_flag &= ~MPRESERVE;
      n_pstate |= n_PS_DID_PRINT_DOT;
   }
   NYD_OU;
   return 0;
}

FL int
c_mboxit(void *v)
{
   int *msgvec = v, *ip;
   NYD_IN;

   if (n_pstate & n_PS_EDIT) {
      n_err(_("mbox: can only be used in a system mailbox\n")); /* TODO */
      goto jleave;
   }

   for (ip = msgvec; *ip != 0; ++ip) {
      setdot(message + *ip - 1);
      dot->m_flag |= MTOUCH | MBOX;
      dot->m_flag &= ~MPRESERVE;
      n_pstate |= n_PS_DID_PRINT_DOT;
   }
jleave:
   NYD_OU;
   return 0;
}

FL int
c_preserve(void *v)
{
   int *msgvec = v, *ip, mesg, rv = 1;
   struct message *mp;
   NYD_IN;

   if (n_pstate & n_PS_EDIT) {
      fprintf(n_stdout, _("preserve: cannot be used in a system mailbox\n"));
      goto jleave;
   }

   for (ip = msgvec; *ip != 0; ++ip) {
      mesg = *ip;
      mp = message + mesg - 1;
      mp->m_flag |= MPRESERVE;
      mp->m_flag &= ~MBOX;
      setdot(mp);
      n_pstate |= n_PS_DID_PRINT_DOT;
   }
   rv = 0;
jleave:
   NYD_OU;
   return rv;
}

FL int
c_unread(void *v)
{
   struct message *mp;
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      mp = &message[*ip - 1];
      setdot(mp);
      dot->m_flag &= ~(MREAD | MTOUCH);
      dot->m_flag |= MSTATUS;
#ifdef mx_HAVE_IMAP
      if (mb.mb_type == MB_IMAP || mb.mb_type == MB_CACHE)
         imap_unread(mp, *ip); /* TODO return? */
#endif
      n_pstate |= n_PS_DID_PRINT_DOT;
   }
   NYD_OU;
   return 0;
}

FL int
c_seen(void *v)
{
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      struct message *mp = message + *ip - 1;
      setdot(mp);
      touch(mp);
   }
   NYD_OU;
   return 0;
}

FL int
c_flag(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MFLAG | MFLAGGED)))
         m->m_flag |= MFLAG | MFLAGGED;
   }
   NYD_OU;
   return 0;
}

FL int
c_unflag(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MFLAG | MFLAGGED)) {
         m->m_flag &= ~(MFLAG | MFLAGGED);
         m->m_flag |= MUNFLAG;
      }
   }
   NYD_OU;
   return 0;
}

FL int
c_answered(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MANSWER | MANSWERED)))
         m->m_flag |= MANSWER | MANSWERED;
   }
   NYD_OU;
   return 0;
}

FL int
c_unanswered(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MANSWER | MANSWERED)) {
         m->m_flag &= ~(MANSWER | MANSWERED);
         m->m_flag |= MUNANSWER;
      }
   }
   NYD_OU;
   return 0;
}

FL int
c_draft(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MDRAFT | MDRAFTED)))
         m->m_flag |= MDRAFT | MDRAFTED;
   }
   NYD_OU;
   return 0;
}

FL int
c_undraft(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_IN;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MDRAFT | MDRAFTED)) {
         m->m_flag &= ~(MDRAFT | MDRAFTED);
         m->m_flag |= MUNDRAFT;
      }
   }
   NYD_OU;
   return 0;
}

#include "su/code-ou.h"
/* s-it-mode */
