/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Folder related user commands.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2016 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define n_FILE cmd_folder

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

/* c_file, c_File */
static int        _c_file(void *v, enum fedit_mode fm);

static int
_c_file(void *v, enum fedit_mode fm)
{
   char **argv = v;
   int i;
   NYD2_ENTER;

   if (*argv == NULL) {
      newfileinfo();
      i = 0;
      goto jleave;
   }

   if (pstate & PS_HOOK_MASK) {
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
   assert(!(fm & FEDIT_NEWMAIL)); /* (Prevent implementation error) */
   if (pstate & PS_SETFILE_OPENED)
      check_folder_hook(FAL0);

   if (i > 0) {
      /* TODO Don't report "no messages" == 1 == error when we're in, e.g.,
       * TODO a macro: because that recursed commando loop will terminate the
       * TODO entire macro due to that!  So either the user needs to be able
       * TODO to react&ignore this "error" (as in "if DOSTUFF" or "DOSTUFF;
       * TODO if $?", then "overriding an "error"), or we need a different
       * TODO return that differentiates */
      i = (pstate & PS_ROBOT) ? 0 : 1;
      goto jleave;
   }
   if (pstate & PS_SETFILE_OPENED)
      announce(ok_blook(bsdcompat) || ok_blook(bsdannounce));
   i = 0;
jleave:
   NYD2_LEAVE;
   return i;
}

FL int
c_file(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _c_file(v, FEDIT_NONE);
   NYD_LEAVE;
   return rv;
}

FL int
c_File(void *v)
{
   int rv;
   NYD_ENTER;

   rv = _c_file(v, FEDIT_RDONLY);
   NYD_LEAVE;
   return rv;
}

FL int
c_newmail(void *v)
{
   int val = 1, mdot;
   NYD_ENTER;
   n_UNUSED(v);

   if (pstate & PS_HOOK_MASK)
      n_err(_("Cannot call `newmail' from within a hook\n"));
   else if ((val = setfile(mailname,
            FEDIT_NEWMAIL | ((mb.mb_perm & MB_DELE) ? 0 : FEDIT_RDONLY))
         ) == 0) {
      mdot = getmdot(1);
      setdot(message + mdot - 1);
   }
   NYD_LEAVE;
   return val;
}

FL int
c_noop(void *v)
{
   int rv = 0;
   NYD_ENTER;
   n_UNUSED(v);

   switch (mb.mb_type) {
   case MB_POP3:
#ifdef HAVE_POP3
      pop3_noop();
#else
      rv = c_cmdnotsupp(NULL);
#endif
      break;
   default:
      break;
   }
   NYD_LEAVE;
   return rv;
}

FL int
c_remove(void *v)
{
   char const *fmt;
   size_t fmt_len;
   char **args, *name, *ename;
   int ec;
   NYD_ENTER;

   if (*(args = v) == NULL) {
      n_err(_("Synopsis: remove: <mailbox>...\n"));
      ec = 1;
      goto jleave;
   }

   ec = 0;

   fmt = _("Remove %s");
   fmt_len = strlen(fmt);
   do {
      if ((name = expand(*args)) == NULL)
         continue;
      ename = n_shexp_quote_cp(name, FAL0);

      if (!strcmp(name, mailname)) {
         n_err(_("Cannot remove current mailbox %s\n"), ename);
         ec |= 1;
         continue;
      }
      {
         size_t vl = strlen(ename) + fmt_len +1;
         char *vb = salloc(vl);
         bool_t asw;
         snprintf(vb, vl, fmt, ename);
         asw = getapproval(vb, TRU1);
         if (!asw)
            continue;
      }

      switch (which_protocol(name)) {
      case PROTO_FILE:
         if (unlink(name) == -1) { /* TODO do not handle .gz .bz2 .xz.. */
            int se = errno;

            if (se == EISDIR) {
               struct stat sb;

               if (!stat(name, &sb) && S_ISDIR(sb.st_mode)) {
                  if (!rmdir(name))
                     break;
                  se = errno;
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
         if (maildir_remove(name) != OKAY)
            ec |= 1;
         break;
      case PROTO_UNKNOWN:
         n_err(_("Not removed: unknown protocol: %s\n"), ename);
         ec |= 1;
         break;
      }
   } while (*++args != NULL);
jleave:
   NYD_LEAVE;
   return ec;
}

FL int
c_rename(void *v)
{
   char **args = v, *old, *new;
   enum protocol oldp, newp;
   int ec;
   NYD_ENTER;

   ec = 1;

   if (args[0] == NULL || args[1] == NULL || args[2] != NULL) {
      n_err(_("Synopsis: rename: <old> <new>\n"));
      goto jleave;
   }

   if ((old = expand(args[0])) == NULL)
      goto jleave;
   oldp = which_protocol(old);
   if ((new = expand(args[1])) == NULL)
      goto jleave;
   newp = which_protocol(new);

   if (!strcmp(old, mailname) || !strcmp(new, mailname)) {
      n_err(_("Cannot rename current mailbox %s\n"),
         n_shexp_quote_cp(old, FAL0));
      goto jleave;
   }

   ec = 0;

   if (newp == PROTO_POP3)
      goto jnopop3;
   switch (oldp) {
   case PROTO_FILE:
      if (link(old, new) == -1) {
         switch (errno) {
         case EACCES:
         case EEXIST:
         case ENAMETOOLONG:
         case ENOENT:
         case ENOSPC:
         case EXDEV:
            n_perr(new, 0);
            break;
         default:
            n_perr(old, 0);
            break;
         }
         ec |= 1;
      } else if (unlink(old) == -1) {
         n_perr(old, 0);
         ec |= 1;
      }
      break;
   case PROTO_MAILDIR:
      if (rename(old, new) == -1) {
         n_perr(old, 0);
         ec |= 1;
      }
      break;
   case PROTO_POP3:
jnopop3:
      n_err(_("Cannot rename POP3 mailboxes\n"));
      ec |= 1;
      break;
   case PROTO_UNKNOWN:
   default:
      n_err(_("Unknown protocol in %s and %s; not renamed\n"),
         n_shexp_quote_cp(old, FAL0), n_shexp_quote_cp(new, FAL0));
      ec |= 1;
      break;
   }
jleave:
   NYD_LEAVE;
   return ec;
}

FL int
c_folders(void *v)
{
   char const *cp;
   char **argv;
   int rv;
   NYD_ENTER;

   rv = 1;

   if(*(argv = v) != NULL){
      if((cp = fexpand(*argv, FEXP_NSHELL | FEXP_LOCAL)) == NULL)
         goto jleave;
   }else
      cp = folder_query();

   rv = run_command(ok_vlook(LISTER), 0, COMMAND_FD_PASS, COMMAND_FD_PASS, cp,
         NULL, NULL, NULL);
   if(rv < 0)
      rv = 1; /* XXX */
jleave:
   NYD_LEAVE;
   return rv;
}

/* s-it-mode */
