/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Iterating over, and over such housekeeping message user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define n_FILE cmd_message

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* Prepare and print "[Message: xy]:" intro */
static void    _show_msg_overview(FILE *obuf, struct message *mp, int msg_no);

/* Show the requested messages */
static int     _type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
                  bool_t donotdecode, char *cmd, ui64_t *tstats);

/* Pipe the requested messages */
static int     _pipe1(char *str, int doign);

/* `top' / `Top' */
static int a_cmsg_top(void *vp, struct n_ignore const *itp);

/* Delete the indicated messages.  Set dot to some nice place afterwards */
static int     delm(int *msgvec);

static void
_show_msg_overview(FILE *obuf, struct message *mp, int msg_no)
{
   char const *cpre, *csuf;
   NYD_ENTER;

   cpre = csuf = n_empty;
#ifdef HAVE_COLOUR
   if (n_pstate & n_PS_COLOUR_ACTIVE) {
      struct n_colour_pen *cpen;

      if ((cpen = n_colour_pen_create(n_COLOUR_ID_VIEW_MSGINFO, NULL)) != NULL){
         struct str const *sp;

         if ((sp = n_colour_pen_to_str(cpen)) != NULL)
            cpre = sp->s;
         if ((sp = n_colour_reset_to_str()) != NULL)
            csuf = sp->s;
      }
   }
#endif
   /* XXX Message info uses wire format for line count */
   fprintf(obuf, _("%s[-- Message %2d -- %lu lines, %lu bytes --]:%s\n"),
      cpre, msg_no, (ul_i)mp->m_lines, (ul_i)mp->m_size, csuf);
   NYD_LEAVE;
}

static int
_type1(int *msgvec, bool_t doign, bool_t dopage, bool_t dopipe,
   bool_t donotdecode, char *cmd, ui64_t *tstats)
{
   struct n_sigman sm;
   ui64_t mstats[1];
   int volatile rv = 1;
   int *ip;
   struct message *mp;
   char const *cp;
   FILE * volatile obuf;
   bool_t volatile isrelax = FAL0;
   NYD_ENTER;
   {/* C89.. */
   enum sendaction const action = ((dopipe && ok_blook(piperaw))
         ? SEND_MBOX : donotdecode
         ? SEND_SHOW : doign
         ? SEND_TODISP : SEND_TODISP_ALL);
   bool_t const volatile formfeed = (dopipe && ok_blook(page));
   obuf = n_stdout;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL) {
   case 0:
      break;
   default:
      goto jleave;
   }

   if (dopipe) {
      if ((obuf = Popen(cmd, "w", ok_vlook(SHELL), NULL, 1)) == NULL) {
         n_perr(cmd, 0);
         obuf = n_stdout;
      }
   } else if ((n_psonce & n_PSO_TTYOUT) && (dopage ||
         ((n_psonce & n_PSO_INTERACTIVE) && (cp = ok_vlook(crt)) != NULL))) {
      size_t nlines = 0;

      if (!dopage) {
         for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
            mp = message + *ip - 1;
            if (!(mp->m_content_info & CI_HAVE_BODY))
               if (get_body(mp) != OKAY)
                  goto jcleanup_leave;
            nlines += mp->m_lines + 1; /* TODO BUT wire format, not display! */
         }
      }

      /* >= not <: we return to the prompt */
      if(dopage || UICMP(z, nlines, >=,
            (*cp != '\0' ? strtoul(cp, NULL, 0) : (size_t)n_realscreenheight))){
         if((obuf = n_pager_open()) == NULL)
            obuf = n_stdout;
      }
#ifdef HAVE_COLOUR
      if ((n_psonce & n_PSO_INTERACTIVE) &&
            (action == SEND_TODISP || action == SEND_TODISP_ALL))
         n_colour_env_create(n_COLOUR_CTX_VIEW, obuf != n_stdout);
#endif
   }
#ifdef HAVE_COLOUR
   else if ((n_psonce & n_PSO_INTERACTIVE) &&
         (action == SEND_TODISP || action == SEND_TODISP_ALL))
      n_colour_env_create(n_COLOUR_CTX_VIEW, FAL0);
#endif

   /*TODO unless we have our signal manager special care must be taken */
   srelax_hold();
   isrelax = TRU1;
   for (ip = msgvec; *ip && PTRCMP(ip - msgvec, <, msgCount); ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      setdot(mp);
      n_pstate |= n_PS_DID_PRINT_DOT;
      uncollapse1(mp, 1);
      if (!dopipe && ip != msgvec)
         fprintf(obuf, "\n");
      if (action != SEND_MBOX)
         _show_msg_overview(obuf, mp, *ip);
      sendmp(mp, obuf, (doign ? n_IGNORE_TYPE : NULL), NULL, action, mstats);
      srelax();
      if (formfeed) /* TODO a nicer way to separate piped messages! */
         putc('\f', obuf);
      if (tstats != NULL)
         tstats[0] += mstats[0];
   }
   srelax_rele();
   isrelax = FAL0;

   rv = 0;
jcleanup_leave:
   n_sigman_cleanup_ping(&sm);
jleave:
   if (isrelax)
      srelax_rele();
   n_COLOUR( n_colour_env_gut((sm.sm_signo != SIGPIPE) ? obuf : NULL); )
   if (obuf != n_stdout)
      n_pager_close(obuf);
   }
   NYD_LEAVE;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

static int
_pipe1(char *str, int doign)
{
   ui64_t stats[1];
   char const *cmd, *cmdq;
   int *msgvec, rv = 1;
   bool_t needs_list;
   NYD_ENTER;

   if ((cmd = laststring(str, &needs_list, TRU1)) == NULL) {
      cmd = ok_vlook(cmd);
      if (cmd == NULL || *cmd == '\0') {
         n_err(_("Variable *cmd* not set\n"));
         goto jleave;
      }
   }

   msgvec = salloc((msgCount + 2) * sizeof *msgvec);

   if (!needs_list) {
      *msgvec = first(0, MMNORM);
      if (*msgvec == 0) {
         if (n_pstate & (n_PS_ROBOT | n_PS_HOOK_MASK)) {
            rv = 0;
            goto jleave;
         }
         fputs(_("No messages to pipe.\n"), n_stdout);
         goto jleave;
      }
      msgvec[1] = 0;
   } else if (getmsglist(str, msgvec, 0) < 0)
      goto jleave;
   if (*msgvec == 0) {
      if (n_pstate & (n_PS_ROBOT | n_PS_HOOK_MASK)) {
         rv = 0;
         goto jleave;
      }
      fprintf(n_stdout, "No applicable messages.\n");
      goto jleave;
   }

   cmdq = n_shexp_quote_cp(cmd, FAL0);
   fprintf(n_stdout, _("Pipe to: %s\n"), cmdq);
   stats[0] = 0;
   if ((rv = _type1(msgvec, doign, FAL0, TRU1, FAL0, n_UNCONST(cmd), stats)
         ) == 0)
      fprintf(n_stdout, "%s %" PRIu64 " bytes\n", cmdq, stats[0]);
jleave:
   NYD_LEAVE;
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
   NYD2_ENTER;

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

#ifdef HAVE_COLOUR
   if (n_psonce & n_PSO_INTERACTIVE)
      n_colour_env_create(n_COLOUR_CTX_VIEW, TRU1);
#endif
   n_string_creat_auto(&s);
   /* C99 */{
      long l;

      if((l = strtol(ok_vlook(toplines), NULL, 0)) <= 0){
         tmax = (size_t)screensize();
         if(l < 0){
            l = n_ABS(l);
            tmax >>= l;
         }
      }else
         tmax = (size_t)l;
   }
   f = ok_blook(topsqueeze) ? a_SQUEEZE : a_NONE;

   for(ip = msgvec = vp;
         *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount); ++ip){
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
      if(sendmp(mp, iobuf, itp, NULL, SEND_TODISP_ALL, NULL) < 0){
         n_err(_("`top': failed to prepare message %d\n"), *ip);
         vp = NULL;
         break;
      }
      fflush_rewind(iobuf);

      _show_msg_overview(pbuf, mp, *ip);
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
               else for(; (c = *cp) != '\0'; ++cp){
                  if(!asciichar(c))
                     break;
                  if(!blankspacechar(c)){
                     if(!ISQUOTE(c))
                        break;
                     c = '\0';
                     break;
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
   n_COLOUR( n_colour_env_gut(pbuf); )

   fflush(pbuf);
   page_or_print(pbuf, plines);

   Fclose(pbuf);
jleave1:
   Fclose(iobuf);
jleave:
   NYD2_LEAVE;
   return (vp != NULL);
}

static int
delm(int *msgvec)
{
   struct message *mp;
   int rv = -1, *ip, last;
   NYD_ENTER;

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
   NYD_LEAVE;
   return rv;
}

FL int
c_more(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, TRU1, TRU1, FAL0, FAL0, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_More(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, FAL0, TRU1, FAL0, FAL0, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_type(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, TRU1, FAL0, FAL0, FAL0, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_Type(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, FAL0, FAL0, FAL0, FAL0, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_show(void *v)
{
   int *msgvec = v, rv;
   NYD_ENTER;

   rv = _type1(msgvec, FAL0, FAL0, FAL0, TRU1, NULL, NULL);
   NYD_LEAVE;
   return rv;
}

FL int
c_pipe(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = _pipe1(str, 1);
   NYD_LEAVE;
   return rv;
}

FL int
c_Pipe(void *v)
{
   char *str = v;
   int rv;
   NYD_ENTER;

   rv = _pipe1(str, 0);
   NYD_LEAVE;
   return rv;
}

FL int
c_top(void *v){
   struct n_ignore *itp;
   int rv;
   NYD_ENTER;

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
   NYD_LEAVE;
   return rv;
}

FL int
c_Top(void *v){
   int rv;
   NYD_ENTER;

   rv = !a_cmsg_top(v, n_IGNORE_TYPE);
   NYD_LEAVE;
   return rv;
}

FL int
c_next(void *v)
{
   int list[2], *ip, *ip2, mdot, *msgvec = v, rv = 1;
   struct message *mp;
   NYD_ENTER;

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
   NYD_LEAVE;
   return rv;
}

FL int
c_pdot(void *vp)
{
   NYD_ENTER;
   n_UNUSED(vp);
   fprintf(n_stdout, "%d\n", (int)PTR2SIZE(dot - message + 1));
   NYD_LEAVE;
   return 0;
}

FL int
c_messize(void *v)
{
   int *msgvec = v, *ip, mesg;
   struct message *mp;
   NYD_ENTER;

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
   NYD_LEAVE;
   return 0;
}

FL int
c_delete(void *v)
{
   int *msgvec = v;
   NYD_ENTER;

   delm(msgvec);
   NYD_LEAVE;
   return 0;
}

FL int
c_deltype(void *v)
{
   int list[2], rv = 0, *msgvec = v, lastdot;
   NYD_ENTER;

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
   NYD_LEAVE;
   return rv;
}

FL int
c_undelete(void *v)
{
   int *msgvec = v, *ip;
   struct message *mp;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0 && UICMP(z, PTR2SIZE(ip - msgvec), <, msgCount);
         ++ip) {
      mp = message + *ip - 1;
      touch(mp);
      setdot(mp);
      if (mp->m_flag & (MDELETED | MSAVED))
         mp->m_flag &= ~(MDELETED | MSAVED);
      else
         mp->m_flag &= ~MDELETED;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_stouch(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      setdot(message + *ip - 1);
      dot->m_flag |= MTOUCH;
      dot->m_flag &= ~MPRESERVE;
      n_pstate |= n_PS_DID_PRINT_DOT;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_mboxit(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

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
   NYD_LEAVE;
   return 0;
}

FL int
c_preserve(void *v)
{
   int *msgvec = v, *ip, mesg, rv = 1;
   struct message *mp;
   NYD_ENTER;

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
   NYD_LEAVE;
   return rv;
}

FL int
c_unread(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      setdot(message + *ip - 1);
      dot->m_flag &= ~(MREAD | MTOUCH);
      dot->m_flag |= MSTATUS;
      n_pstate |= n_PS_DID_PRINT_DOT;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_seen(void *v)
{
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      struct message *mp = message + *ip - 1;
      setdot(mp);
      touch(mp);
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_flag(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MFLAG | MFLAGGED)))
         m->m_flag |= MFLAG | MFLAGGED;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_unflag(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MFLAG | MFLAGGED)) {
         m->m_flag &= ~(MFLAG | MFLAGGED);
         m->m_flag |= MUNFLAG;
      }
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_answered(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MANSWER | MANSWERED)))
         m->m_flag |= MANSWER | MANSWERED;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_unanswered(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MANSWER | MANSWERED)) {
         m->m_flag &= ~(MANSWER | MANSWERED);
         m->m_flag |= MUNANSWER;
      }
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_draft(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (!(m->m_flag & (MDRAFT | MDRAFTED)))
         m->m_flag |= MDRAFT | MDRAFTED;
   }
   NYD_LEAVE;
   return 0;
}

FL int
c_undraft(void *v)
{
   struct message *m;
   int *msgvec = v, *ip;
   NYD_ENTER;

   for (ip = msgvec; *ip != 0; ++ip) {
      m = message + *ip - 1;
      setdot(m);
      if (m->m_flag & (MDRAFT | MDRAFTED)) {
         m->m_flag &= ~(MDRAFT | MDRAFTED);
         m->m_flag |= MUNDRAFT;
      }
   }
   NYD_LEAVE;
   return 0;
}

/* s-it-mode */
