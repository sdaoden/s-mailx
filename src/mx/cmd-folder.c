/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Folder related user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_folder
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>

#include "mx/child.h"
#include "mx/tty.h"

/* TODO fake */
#include "su/code-in.h"

/* c_file, c_File */
static int        _c_file(void *v, enum fedit_mode fm);

static int
_c_file(void *v, enum fedit_mode fm)
{
   char **argv = v;
   int i;
   NYD2_IN;

   if(*argv == NULL){
      n_folder_announce(n_ANNOUNCE_STATUS);
      i = 0;
      goto jleave;
   }

   if (n_pstate & n_PS_HOOK_MASK) {
      n_err(_("Cannot change folder from within a hook\n"));
      i = 1;
      goto jleave;
   }

   save_mbox_for_possible_quitstuff();

   i = setfile(*argv, fm);
   if (i < 0) {
      i = 1;
      goto jleave;
   }
   ASSERT(!(fm & FEDIT_NEWMAIL)); /* (Prevent implementation error) */
   if (n_pstate & n_PS_SETFILE_OPENED)
      temporary_folder_hook_check(FAL0);

   if (i > 0) {
      /* TODO Don't report "no messages" == 1 == error when we're in, e.g.,
       * TODO a macro: because that recursed commando loop will terminate the
       * TODO entire macro due to that!  So either the user needs to be able
       * TODO to react&ignore this "error" (as in "if DOSTUFF" or "DOSTUFF;
       * TODO if $?", then "overriding an "error"), or we need a different
       * TODO return that differentiates */
      i = (n_pstate & n_PS_ROBOT) ? 0 : 1;
      goto jleave;
   }

   if(n_pstate & n_PS_SETFILE_OPENED)
      n_folder_announce(n_ANNOUNCE_CHANGE);
   i = 0;
jleave:
   NYD2_OU;
   return i;
}

FL int
c_file(void *v)
{
   int rv;
   NYD_IN;

   rv = _c_file(v, FEDIT_NONE);
   NYD_OU;
   return rv;
}

FL int
c_File(void *v)
{
   int rv;
   NYD_IN;

   rv = _c_file(v, FEDIT_RDONLY);
   NYD_OU;
   return rv;
}

FL int
c_newmail(void *v)
{
   int val = 1, mdot;
   NYD_IN;
   UNUSED(v);

   if (n_pstate & n_PS_HOOK_MASK)
      n_err(_("Cannot call `newmail' from within a hook\n"));
#ifdef mx_HAVE_IMAP
   else if(mb.mb_type == MB_IMAP && !imap_newmail(1))
      ;
#endif
   else if ((val = setfile(mailname,
            FEDIT_NEWMAIL | ((mb.mb_perm & MB_DELE) ? 0 : FEDIT_RDONLY))
         ) == 0) {
      mdot = getmdot(1);
      setdot(message + mdot - 1);
   }
   NYD_OU;
   return val;
}

FL int
c_noop(void *v)
{
   int rv = 0;
   NYD_IN;
   UNUSED(v);

   switch (mb.mb_type) {
#ifdef mx_HAVE_POP3
   case MB_POP3:
      pop3_noop();
      break;
#endif
#ifdef mx_HAVE_IMAP
   case MB_IMAP:
      imap_noop();
      break;
#endif
   default:
      break;
   }
   NYD_OU;
   return rv;
}

FL int
c_remove(void *v)
{
   char const *fmt;
   uz fmt_len;
   char **args, *name, *ename;
   int ec;
   NYD_IN;

   if (*(args = v) == NULL) {
      n_err(_("Synopsis: remove: <mailbox>...\n"));
      ec = 1;
      goto jleave;
   }

   ec = 0;

   fmt = _("Remove %s");
   fmt_len = su_cs_len(fmt);
   do {
      if ((name = fexpand(*args, FEXP_FULL)) == NULL)
         continue;
      ename = n_shexp_quote_cp(name, FAL0);

      if (!su_cs_cmp(name, mailname)) {
         n_err(_("Cannot remove current mailbox %s\n"), ename);
         ec |= 1;
         continue;
      }
      /* C99 */{
         boole asw;
         char *vb;
         uz vl;

         vl = su_cs_len(ename) + fmt_len +1;
         vb = n_autorec_alloc(vl);
         snprintf(vb, vl, fmt, ename);
         asw = mx_tty_yesorno(vb, TRU1);
         if(!asw)
            continue;
      }

      switch (which_protocol(name, TRU1, FAL0, NULL)) {
      case PROTO_FILE:
         if (unlink(name) == -1) {
            int se = su_err_no();

            if (se == su_ERR_ISDIR) {
               struct stat sb;

               if (!stat(name, &sb) && S_ISDIR(sb.st_mode)) {
                  if (!rmdir(name))
                     break;
                  se = su_err_no();
               }
            }
            n_perr(name, se);
            ec |= 1;
         }
         break;
      case PROTO_POP3:
         n_err(_("Cannot remove POP3 mailbox %s\n"), ename);
         ec |= 1;
         break;
      case PROTO_MAILDIR:
#ifdef mx_HAVE_MAILDIR
         if(maildir_remove(name) != OKAY)
            ec |= 1;
#else
         n_err(_("No Maildir directory support compiled in\n"));
         ec |= 1;
#endif
         break;
      case PROTO_IMAP:
#ifdef mx_HAVE_IMAP
         if(imap_remove(name) != OKAY)
            ec |= 1;
#else
         n_err(_("No IMAP support compiled in\n"));
         ec |= 1;
#endif
         break;
      case PROTO_UNKNOWN:
      default:
         n_err(_("Not removed: unknown protocol: %s\n"), ename);
         ec |= 1;
         break;
      }
   } while (*++args != NULL);
jleave:
   NYD_OU;
   return ec;
}

FL int
c_rename(void *v)
{
   char **args = v, *oldn, *newn;
   enum protocol oldp;
   int ec;
   NYD_IN;

   ec = 1;

   if (args[0] == NULL || args[1] == NULL || args[2] != NULL) {
      n_err(_("Synopsis: rename: <old> <new>\n"));
      goto jleave;
   }

   if ((oldn = fexpand(args[0], FEXP_FULL)) == NULL)
      goto jleave;
   oldp = which_protocol(oldn, TRU1, FAL0, NULL);
   if ((newn = fexpand(args[1], FEXP_FULL)) == NULL)
      goto jleave;
   if(oldp != which_protocol(newn, TRU1, FAL0, NULL)) {
      n_err(_("Can only rename folders of same type\n"));
      goto jleave;
   }
   if (!su_cs_cmp(oldn, mailname) || !su_cs_cmp(newn, mailname)) {
      n_err(_("Cannot rename current mailbox %s\n"),
         n_shexp_quote_cp(oldn, FAL0));
      goto jleave;
   }

   ec = 0;

   switch (oldp) {
   case PROTO_FILE:
      if (link(oldn, newn) == -1) {
         switch (su_err_no()) {
         case su_ERR_ACCES:
         case su_ERR_EXIST:
         case su_ERR_NAMETOOLONG:
         case su_ERR_NOSPC:
         case su_ERR_XDEV:
            n_perr(newn, 0);
            break;
         default:
            n_perr(oldn, 0);
            break;
         }
         ec |= 1;
      } else if (unlink(oldn) == -1) {
         n_perr(oldn, 0);
         ec |= 1;
      }
      break;
   case PROTO_MAILDIR:
#ifdef mx_HAVE_MAILDIR
      if(rename(oldn, newn) == -1){
         n_perr(oldn, 0);
         ec |= 1;
      }
#else
      n_err(_("No Maildir directory support compiled in\n"));
      ec |= 1;
#endif
      break;
   case PROTO_POP3:
      n_err(_("Cannot rename POP3 mailboxes\n"));
      ec |= 1;
      break;
   case PROTO_IMAP:
#ifdef mx_HAVE_IMAP
      if(imap_rename(oldn, newn) != OKAY)
         ec |= 1;
#else
      n_err(_("No IMAP support compiled in\n"));
      ec |= 1;
#endif
      break;
   case PROTO_UNKNOWN:
   default:
      n_err(_("Unknown protocol in %s and %s; not renamed\n"),
         n_shexp_quote_cp(oldn, FAL0), n_shexp_quote_cp(newn, FAL0));
      ec |= 1;
      break;
   }
jleave:
   NYD_OU;
   return ec;
}

FL int
c_folders(void *v){ /* TODO fexpand*/
   enum fexp_mode const fexp = FEXP_NSHELL
#ifndef mx_HAVE_IMAP
         | FEXP_LOCAL
#endif
      ;
   struct mx_child_ctx cc;
   char const *cp;
   char **argv;
   int rv;
   NYD_IN;

   rv = 1;

   if(*(argv = v) != NULL){
      if((cp = fexpand(*argv, fexp)) == NIL)
         goto jleave;
   }else
      cp = n_folder_query();

#ifdef mx_HAVE_IMAP
   if(which_protocol(cp, FAL0, FAL0, NIL) == PROTO_IMAP)
      rv = imap_folders(cp, *argv == NIL);
   else
#endif
       {
      mx_child_ctx_setup(&cc);
      cc.cc_flags = mx_CHILD_RUN_WAIT_LIFE;
      cc.cc_cmd = ok_vlook(LISTER);
      cc.cc_args[0] = cp;
      if(mx_child_run(&cc) && cc.cc_exit_status == 0)
         rv = 0;
   }

jleave:
   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
/* s-it-mode */
