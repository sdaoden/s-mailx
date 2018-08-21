/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Iterating over, and over such housekeeping message user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#undef n_FILE
#define n_FILE cmd_msg

#ifndef HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

/* Prepare and print "[Message: xy]:" intro */
static bool_t a_cmsg_show_overview(FILE *obuf, struct message *mp, int msg_no);

/* Show the requested messages */
static int     _type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
                  bool_t donotdecode, char *cmd, ui64_t *tstats);

/* Pipe the requested messages */
static int a_cmsg_pipe1(void *vp, bool_t doign);

/* `top' / `Top' */
static int a_cmsg_top(void *vp, struct n_ignore const *itp);

/* Delete the indicated messages.  Set dot to some nice place afterwards */
static int     delm(int *msgvec);

static bool_t
a_cmsg_show_overview(FILE *obuf, struct message *mp, int msg_no){
   bool_t rv;
   char const *cpre, *csuf;
   NYD2_IN;

   cpre = csuf = n_empty;
#ifdef HAVE_COLOUR
   if(n_COLOUR_IS_ACTIVE()){
      struct n_colour_pen *cpen;

      if((cpen = n_colour_pen_create(n_COLOUR_ID_VIEW_MSGINFO, NULL)) != NULL){
         struct str const *sp;

         if((sp = n_colour_pen_to_str(cpen)) != NULL)
            cpre = sp->s;
         if((sp = n_colour_reset_to_str()) != NULL)
            csuf = sp->s;
      }
   }
#endif
   /* XXX Message info uses wire format for line count */
   rv = (fprintf(obuf,
         A_("%s[-- Message %2d -- %lu lines, %lu bytes --]:%s\n"),
         cpre, msg_no, (ul_i)mp->m_lines, (ul_i)mp->m_size, csuf) > 0);
   NYD2_OU;
   return rv;
}

static int
_type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
   bool_t donotdecode, char *cmd, ui64_t *tstats)
{
   ui64_t mstats[1];
   int *ip;
   struct message *mp;
   char const *cp;
   enum sendaction action;
   bool_t volatile formfeed;
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

   if (dopipe) {
      if ((obuf = Popen(cmd, "w", ok_vlook(SHELL), NULL, 1)) == NULL) {
         n_perr(cmd, 0);
         obuf = n_stdout;
      }
   } else if ((n_psonce & n_PSO_TTYOUT) && (dopage ||
         ((n_psonce & n_PSO_INTERACTIVE) && (cp = ok_vlook(crt)) != NULL))) {
      uiz_t nlines, lib;

      nlines = 0;

      if (!dopage) {
         for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
            mp = message + *ip - 1;
            if (!(mp->m_content_info & CI_HAVE_BODY))
               if (get_body(mp) != OKAY)
                  goto jleave;
            nlines += mp->m_lines + 1; /* TODO BUT wire format, not display! */
         }
      }

      /* >= not <: we return to the prompt */
      if(dopage || nlines >= (*cp != '\0'
               ? (n_idec_uiz_cp(&lib, cp, 0, NULL), lib)
               : (uiz_t)n_realscreenheight)){
         if((obuf = n_pager_open()) == NULL)
            obuf = n_stdout;
      }
      n_COLOUR(
         if(action == SEND_TODISP || action == SEND_TODISP_ALL)
            n_colour_env_create(n_COLOUR_CTX_VIEW, obuf, obuf != n_stdout);
      )
   }
   n_COLOUR(
      else if(action == SEND_TODISP || action == SEND_TODISP_ALL)
         n_colour_env_create(n_COLOUR_CTX_VIEW, n_stdout, FAL0);
   )

   rv = 0;
   srelax_hold();
   for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
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
      if(sendmp(mp, obuf, (doign ? n_IGNORE_TYPE : NULL), NULL, action, mstats
            ) < 0){
         rv = 1;
         break;
      }
      srelax();
      if(formfeed){ /* TODO a nicer way to separate piped messages! */
         if(putc('\f', obuf) == EOF){
            rv = 1;
            break;
         }
      }
      if (tstats != NULL)
         tstats[0] += mstats[0];
   }
   srelax_rele();
   n_COLOUR(
      if(!dopipe && (action == SEND_TODISP || action == SEND_TODISP_ALL))
         n_colour_env_gut();
   )
jleave:
   if (obuf != n_stdout)
      n_pager_close(obuf);
   NYD_OU;
   return rv;
}

static int
a_cmsg_pipe1(void *vp, bool_t doign){
   ui64_t stats[1];
   char const *cmd, *cmdq;
   int *msgvec, rv;
   struct n_cmd_arg *cap;
   struct n_cmd_arg_ctx *cacp;
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
a_cmsg_top(void *vp, struct n_ignore const *itp){
   struct n_string s;
   int *msgvec, *ip;
   enum{a_NONE, a_SQUEEZE = 1u<<0,
      a_EMPTY = 1u<<8, a_STOP = 1u<<9,  a_WORKMASK = 0xFF00u} f;
   size_t tmax, plines;
   FILE *iobuf, *pbuf;
   NYD2_IN;

   if((iobuf = Ftmp(NULL, "topio", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("`top': I/O temporary file"), 0);
      vp = NULL;
      goto jleave;
   }
   if((pbuf = Ftmp(NULL, "toppag", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL){
      n_perr(_("`top': temporary pager file"), 0);
      vp = NULL;
      goto jleave1;
   }

   /* TODO In v15 we should query the m_message object, and directly send only
    * TODO those parts, optionally over empty-line-squeeze and quote-strip
    * TODO filters, in which we are interested in: only text content!
    * TODO And: with *topsqueeze*, header/content separating empty line.. */
   n_pstate &= ~n_PS_MSGLIST_DIRECT; /* TODO NO ATTACHMENTS */
   plines = 0;

   n_COLOUR( n_colour_env_create(n_COLOUR_CTX_VIEW, iobuf, FAL0); )
   n_string_creat_auto(&s);
   /* C99 */{
      siz_t l;

      if((n_idec_siz_cp(&l, ok_vlook(toplines), 0, NULL
               ) & (n_IDEC_STATE_EMASK | n_IDEC_STATE_CONSUMED)
            ) != n_IDEC_STATE_CONSUMED)
         l = 0;
      if(l <= 0){
         tmax = n_screensize();
         if(l < 0){
            l = n_ABS(l);
            tmax >>= l;
         }
      }else
         tmax = (size_t)l;
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
         n_perr(_("`top': ftruncate(2)"), 0);
         vp = NULL;
         break;
      }

      if(!a_cmsg_show_overview(iobuf, mp, *ip) ||
            sendmp(mp, iobuf, itp, NULL, SEND_TODISP_ALL, NULL) < 0){
         n_err(_("`top': failed to prepare message %d\n"), *ip);
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
         size_t l;

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
                     (xcp = strstr(cp, "[-- ")) != NULL &&
                      strstr(&xcp[1], " --]") != NULL)
                  c = '\0';
               else{
                  char const *qcp;

                  for(qcp = ok_vlook(quote_chars); (c = *cp) != '\0'; ++cp){
                     if(!asciichar(c))
                        break;
                     if(!blankspacechar(c)){
                        if(strchr(qcp, c) == NULL)
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
   n_COLOUR( n_colour_env_gut(); )

   fflush(pbuf);
   page_or_print(pbuf, plines);

   Fclose(pbuf);
jleave1:
   Fclose(iobuf);
jleave:
   NYD2_OU;
   return (vp != NULL);
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
      n_err(_("`mimeview': can yet only take one message, sorry!\n"));/* TODO */
      n_pstate_err_no = n_ERR_NOTSUP;
      rv = 1;
      goto jleave;
   }

   mp = &message[*msgvec - 1];
   touch(mp);
   setdot(mp);
   n_pstate |= n_PS_DID_PRINT_DOT;
   uncollapse1(mp, 1);

   n_COLOUR(
      n_colour_env_create(n_COLOUR_CTX_VIEW, n_stdout, FAL0);
   )

   if(!a_cmsg_show_overview(n_stdout, mp, *msgvec))
      n_pstate_err_no = n_ERR_IO;
   else if(sendmp(mp, n_stdout, n_IGNORE_TYPE, NULL, SEND_TODISP_PARTS,
         NULL) < 0)
      n_pstate_err_no = n_ERR_IO;
   else
      n_pstate_err_no = n_ERR_NONE;

   n_COLOUR(
      n_colour_env_gut();
   )

   rv = (n_pstate_err_no != n_ERR_NONE);
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
   struct n_ignore *itp;
   int rv;
   NYD_IN;

   if(n_ignore_is_any(n_IGNORE_TOP))
      itp = n_IGNORE_TOP;
   else{
      itp = n_ignore_new(TRU1);
      n_ignore_insert(itp, TRU1, "from", sizeof("from") -1);
      n_ignore_insert(itp, TRU1, "to", sizeof("to") -1);
      n_ignore_insert(itp, TRU1, "cc", sizeof("cc") -1);
      n_ignore_insert(itp, TRU1, "subject", sizeof("subject") -1);
   }

   rv = !a_cmsg_top(v, itp);
   NYD_OU;
   return rv;
}

FL int
c_Top(void *v){
   int rv;
   NYD_IN;

   rv = !a_cmsg_top(v, n_IGNORE_TYPE);
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
      mdot = (int)PTR2SIZE(dot - message + 1);

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
            PTRCMP(mp, <, message + msgCount); ++mp)
         if (!(mp->m_flag & MMNORM))
            break;
   } else {
      /* TODO The threading code had some bugs that caused crashes.
       * TODO The last thing (before the deep look) happens here,
       * TODO so let's not trust n_PS_DID_PRINT_DOT but check & hope it fixes */
      if ((mp = dot) != NULL && (n_pstate & n_PS_DID_PRINT_DOT))
         mp = next_in_thread(mp);
      while (mp != NULL && (mp->m_flag & MMNORM))
         mp = next_in_thread(mp);
   }
   if (mp == NULL || PTRCMP(mp, >=, message + msgCount)) {
jateof:
      fprintf(n_stdout, _("At EOF\n"));
      rv = 0;
      goto jleave;
   }
   setdot(mp);

   /* Print dot */
jhitit:
   list[0] = (int)PTR2SIZE(dot - message + 1);
   list[1] = 0;
   rv = c_type(list);
jleave:
   NYD_OU;
   return rv;
}

FL int
c_pdot(void *vp){
   char cbuf[n_IENC_BUFFER_SIZE], sep1, sep2;
   struct n_string s, *sp;
   int *mlp;
   struct n_cmd_arg_ctx *cacp;
   NYD_IN;
   n_UNUSED(vp);

   n_pstate_err_no = n_ERR_NONE;
   sp = n_string_creat_auto(&s);
   sep1 = *ok_vlook(ifs);
   sep2 = *ok_vlook(ifs_ws);
   if(sep1 == sep2)
      sep2 = '\0';
   if(sep1 == '\0')
      sep1 = ' ';

   cacp = vp;

   for(mlp = cacp->cac_arg->ca_arg.ca_msglist; *mlp != 0; ++mlp){
      if(!n_string_can_book(sp, n_IENC_BUFFER_SIZE + 2u)){
         n_err(_("`=': overflow: string too long!\n"));
         n_pstate_err_no = n_ERR_OVERFLOW;
         vp = NULL;
         goto jleave;
      }
      if(sp->s_len > 0){
         sp = n_string_push_c(sp, sep1);
         if(sep2 != '\0')
            sp = n_string_push_c(sp, sep2);
      }
      sp = n_string_push_cp(sp,
            n_ienc_buf(cbuf, (ui32_t)*mlp, 10, n_IENC_MODE_NONE));
   }

   (void)n_string_cp(sp);
   if(cacp->cac_vput == NULL){
      if(fprintf(n_stdout, "%s\n", sp->s_dat) < 0){
         n_pstate_err_no = n_err_no;
         vp = NULL;
      }
   }else if(!n_var_vset(cacp->cac_vput, (uintptr_t)sp->s_dat)){
      n_pstate_err_no = n_ERR_NOTSUP;
      vp = NULL;
   }
jleave:
   /* n_string_gut(sp); */
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
      fprintf(n_stdout, "/%lu\n", (ul_i)mp->m_xsize);
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

   lastdot = (int)PTR2SIZE(dot - message + 1);
   if (delm(msgvec) >= 0) {
      list[0] = (int)PTR2SIZE(dot - message + 1);
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
#ifdef HAVE_IMAP
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
      n_err(_("`mbox' can only be used in a system mailbox\n")); /* TODO */
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
      fprintf(n_stdout, _("Cannot `preserve' in a system mailbox\n"));
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
#ifdef HAVE_IMAP
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

/* s-it-mode */
