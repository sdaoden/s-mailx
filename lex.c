/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ (Lexical processing of) Commands, and the event loops.
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
#define n_FILE lex

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

struct cmd {
   char const     *name;         /* Name of command */
   int            (*func)(void*); /* Implementor of command */
   enum argtype   argtype;       /* Arglist type (see below) */
   short          msgflag;       /* Required flags of msgs */
   short          msgmask;       /* Relevant flags of msgs */
#ifdef HAVE_DOCSTRINGS
   char const     *doc;          /* One line doc for command */
#endif
};
/* Yechh, can't initialize unions */
#define minargs   msgflag        /* Minimum argcount for RAWLIST */
#define maxargs   msgmask        /* Max argcount for RAWLIST */

struct cmd_ghost {
   struct cmd_ghost  *next;
   struct str        cmd;        /* Data follows after .name */
   char              name[VFIELD_SIZE(0)];
};

static int              _reset_on_stop;   /* do a reset() if stopped */
static sighandler_type  _oldpipe;
static struct cmd_ghost *_cmd_ghosts;
/* _cmd_tab[] after fun protos */

/* Isolate the command from the arguments */
static char *  _lex_isolate(char const *comm);

/* Get first-fit, or NULL */
static struct cmd const * _lex(char const *comm);

/* Command ghost handling */
static int     _c_ghost(void *v);
static int     _c_unghost(void *v);

/* Print a list of all commands */
static int     _c_list(void *v);

static int     __pcmd_cmp(void const *s1, void const *s2);

/* `quit' command */
static int     _c_quit(void *v);

/* Print the binaries compiled-in features */
static int     _c_features(void *v);

/* Print the binaries version number */
static int     _c_version(void *v);

/* When we wake up after ^Z, reprint the prompt */
static void    stop(int s);

/* Branch here on hangup signal and simulate "exit" */
static void    hangup(int s);

/* List of all commands, and list of commands which are specially treated
 * and deduced in execute(), but we need a list for _c_list() and
 * print_comm_docstr() */
#ifdef HAVE_DOCSTRINGS
# define DS(S)       , S
#else
# define DS(S)
#endif
static struct cmd const _cmd_tab[] = {
#include "cmd_tab.h"
},
                        _special_cmd_tab[] = {
   { "#", NULL, 0, 0, 0
     DS(N_("\"Comment command\": ignore remaining (continuable) line")) },
  { "-", NULL, 0, 0, 0
     DS(N_("Print out the preceding message")) }
};
#undef DS

static char *
_lex_isolate(char const *comm)
{
   NYD_ENTER;
   while (*comm != '\0' &&
         strchr("~|? \t0123456789&%@$^.:/-+*'\",;(`", *comm) == NULL)
      ++comm;
   NYD_LEAVE;
   return UNCONST(comm);
}

static struct cmd const *
_lex(char const *comm) /* TODO **command hashtable**! linear list search!!! */
{
   struct cmd const *cp, *cpmax;
   NYD_ENTER;

   for (cp = cpmax = _cmd_tab, cpmax += NELEM(_cmd_tab); cp != cpmax; ++cp)
      if (*comm == *cp->name && is_prefix(comm, cp->name))
         goto jleave;
   cp = NULL;
jleave:
   NYD_LEAVE;
   return cp;
}

static int
_c_ghost(void *v)
{
   char const **argv = v;
   struct cmd_ghost *lcg, *cg;
   size_t i, cl, nl;
   char *cp;
   NYD_ENTER;

   /* Show the list? */
   if (*argv == NULL) {
      FILE *fp;

      if ((fp = Ftmp(NULL, "ghost", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
         fp = stdout;
      for (i = 0, cg = _cmd_ghosts; cg != NULL; cg = cg->next)
         fprintf(fp, "ghost %s \"%s\"\n", cg->name, string_quote(cg->cmd.s));
      if (fp != stdout) {
         page_or_print(fp, i);
         Fclose(fp);
      }
      goto jleave;
   }

   /* Verify the ghost name is a valid one */
   if (*argv[0] == '\0' || *_lex_isolate(argv[0]) != '\0') {
      n_err(_("`ghost': can't canonicalize \"%s\"\n"), argv[0]);
      v = NULL;
      goto jleave;
   }

   /* Show command of single ghost? */
   if (argv[1] == NULL) {
      for (cg = _cmd_ghosts; cg != NULL; cg = cg->next)
         if (!strcmp(argv[0], cg->name)) {
            printf("ghost %s \"%s\"\n", cg->name, string_quote(cg->cmd.s));
            goto jleave;
         }
      n_err(_("`ghost': no such alias: \"%s\"\n"), argv[0]);
      v = NULL;
      goto jleave;
   }

   /* Define command for ghost: verify command content */
   for (cl = 0, i = 1; (cp = UNCONST(argv[i])) != NULL; ++i)
      if (*cp != '\0')
         cl += strlen(cp) + 1; /* SP or NUL */
   if (cl == 0) {
      n_err(_("`ghost': empty command arguments after \"%s\"\n"), argv[0]);
      v = NULL;
      goto jleave;
   }

   /* If the ghost already exists, recreate */
   for (lcg = NULL, cg = _cmd_ghosts; cg != NULL; lcg = cg, cg = cg->next)
      if (!strcmp(cg->name, argv[0])) {
         if (lcg != NULL)
            lcg->next = cg->next;
         else
            _cmd_ghosts = cg->next;
         free(cg);
         break;
      }

   nl = strlen(argv[0]) +1;
   cg = smalloc(sizeof(*cg) - VFIELD_SIZEOF(struct cmd_ghost, name) + nl + cl);
   cg->next = _cmd_ghosts;
      _cmd_ghosts = cg;
   memcpy(cg->name, argv[0], nl);
   cp = cg->cmd.s = cg->name + nl;
   cg->cmd.l = --cl;
   while (*++argv != NULL) {
      i = strlen(*argv);
      if (i > 0) {
         memcpy(cp, *argv, i);
         cp += i;
         *cp++ = ' ';
      }
   }
   *--cp = '\0';
jleave:
   NYD_LEAVE;
   return (v == NULL);
}

static int
_c_unghost(void *v)
{
   int rv = 0;
   char const **argv = v, *cp;
   struct cmd_ghost *lcg, *cg;
   NYD_ENTER;

   while ((cp = *argv++) != NULL) {
      if (cp[0] == '*' && cp[1] == '\0') {
         while ((cg = _cmd_ghosts) != NULL) {
            _cmd_ghosts = cg->next;
            free(cg);
         }
      } else {
         for (lcg = NULL, cg = _cmd_ghosts; cg != NULL; lcg = cg, cg = cg->next)
            if (!strcmp(cg->name, cp)) {
               if (lcg != NULL)
                  lcg->next = cg->next;
               else
                  _cmd_ghosts = cg->next;
               free(cg);
               goto jouter;
            }
         n_err(_("`unghost': no such alias: \"%s\"\n"), cp);
         rv = 1;
jouter:
         ;
      }
   }
   NYD_LEAVE;
   return rv;
}

static int
__pcmd_cmp(void const *s1, void const *s2)
{
   struct cmd const * const *c1 = s1, * const *c2 = s2;
   int rv;
   NYD_ENTER;

   rv = strcmp((*c1)->name, (*c2)->name);
   NYD_LEAVE;
   return rv;
}

static int
_c_list(void *v)
{
   struct cmd const **cpa, *cp, **cursor;
   size_t i;
   NYD_ENTER;
   UNUSED(v);

   i = NELEM(_cmd_tab) + NELEM(_special_cmd_tab) + 1;
   cpa = ac_alloc(sizeof(cp) * i);

   for (i = 0; i < NELEM(_cmd_tab); ++i)
      cpa[i] = _cmd_tab + i;
   {
      size_t j;

      for (j = 0; j < NELEM(_special_cmd_tab); ++i, ++j)
         cpa[i] = _special_cmd_tab + j;
   }
   cpa[i] = NULL;

   qsort(cpa, i, sizeof(cp), &__pcmd_cmp);

   printf(_("Commands are:\n"));
   for (i = 0, cursor = cpa; (cp = *cursor++) != NULL;) {
      size_t j;
      if (cp->func == &c_cmdnotsupp)
         continue;
      j = strlen(cp->name) + 2;
      if ((i += j) > 72) {
         i = j;
         printf("\n");
      }
      printf((*cursor != NULL ? "%s, " : "%s\n"), cp->name);
   }

   ac_free(cpa);
   NYD_LEAVE;
   return 0;
}

static int
_c_quit(void *v)
{
   int rv;
   NYD_ENTER;
   UNUSED(v);

   /* If we are PS_SOURCING, then return 1 so evaluate() can handle it.
    * Otherwise return -1 to abort command loop */
   rv = (pstate & PS_SOURCING) ? 1 : -1;
   NYD_LEAVE;
   return rv;
}

static int
_c_features(void *v)
{
   NYD_ENTER;
   UNUSED(v);
   printf(_("Features: %s\n"), ok_vlook(features));
   NYD_LEAVE;
   return 0;
}

static int
_c_version(void *v)
{
   NYD_ENTER;
   UNUSED(v);
   printf(_("Version %s\n"), ok_vlook(version));
   NYD_LEAVE;
   return 0;
}

static void
stop(int s)
{
   sighandler_type old_action;
   sigset_t nset;
   NYD_X; /* Signal handler */

   old_action = safe_signal(s, SIG_DFL);

   sigemptyset(&nset);
   sigaddset(&nset, s);
   sigprocmask(SIG_UNBLOCK, &nset, NULL);
   n_raise(s);
   sigprocmask(SIG_BLOCK, &nset, NULL);
   safe_signal(s, old_action);
   if (_reset_on_stop) {
      _reset_on_stop = 0;
      n_TERMCAP_RESUME(TRU1);
      reset(0);
   }
}

static void
hangup(int s)
{
   NYD_X; /* Signal handler */
   UNUSED(s);
   /* nothing to do? */
   exit(EXIT_ERR);
}

FL bool_t
commands(void)
{
   struct eval_ctx ev;
   int n;
   bool_t volatile rv = TRU1;
   NYD_ENTER;

   if (!(pstate & PS_SOURCING)) {
      if (safe_signal(SIGINT, SIG_IGN) != SIG_IGN)
         safe_signal(SIGINT, onintr);
      if (safe_signal(SIGHUP, SIG_IGN) != SIG_IGN)
         safe_signal(SIGHUP, hangup);
      /* TODO We do a lot of redundant signal handling, especially
       * TODO with the command line editor(s); try to merge this */
      safe_signal(SIGTSTP, stop);
      safe_signal(SIGTTOU, stop);
      safe_signal(SIGTTIN, stop);
   }
   _oldpipe = safe_signal(SIGPIPE, SIG_IGN);
   safe_signal(SIGPIPE, _oldpipe);

   memset(&ev, 0, sizeof ev);

   setexit();
   for (;;) {
      char *temporary_orig_line; /* XXX eval_ctx.ev_line not yet constant */

      n_COLOUR( n_colour_env_pop(TRU1); )

      /* TODO Unless we have our signal manager (or however we do it) child
       * TODO processes may have time slots where their execution isn't
       * TODO protected by signal handlers (in between start and setup
       * TODO completed).  close_all_files() is only called from onintr()
       * TODO so those may linger possibly forever */
      if (!(pstate & PS_SOURCING))
         close_all_files();

      interrupts = 0;
      handlerstacktop = NULL;

      temporary_localopts_free(); /* XXX intermediate hack */
      sreset((pstate & PS_SOURCING) != 0);
      if (!(pstate & PS_SOURCING)) {
         char *cp;

         /* TODO Note: this buffer may contain a password.  We should redefine
          * TODO the code flow which has to do that */
         if ((cp = termios_state.ts_linebuf) != NULL) {
            termios_state.ts_linebuf = NULL;
            termios_state.ts_linesize = 0;
            free(cp); /* TODO pool give-back */
         }
         /* TODO Due to expand-on-tab of NCL the buffer may grow */
         if (ev.ev_line.l > LINESIZE * 3) {
            free(ev.ev_line.s); /* TODO pool! but what? */
            ev.ev_line.s = NULL;
            ev.ev_line.l = ev.ev_line_size = 0;
         }
      }

      if (!(pstate & PS_SOURCING) && (options & OPT_INTERACTIVE)) {
         char *cp;

         cp = ok_vlook(newmail);
         if ((options & OPT_TTYIN) && cp != NULL) {
            struct stat st;

            n = (cp != NULL && strcmp(cp, "nopoll"));
            if ((mb.mb_type == MB_FILE && !stat(mailname, &st) &&
                     st.st_size > mailsize) ||
                  (mb.mb_type == MB_MAILDIR && n != 0)) {
               size_t odot = PTR2SIZE(dot - message);
               ui32_t odid = (pstate & PS_DID_PRINT_DOT);

               if (setfile(mailname,
                     FEDIT_NEWMAIL |
                     ((mb.mb_perm & MB_DELE) ? 0 : FEDIT_RDONLY)) < 0) {
                  exit_status |= EXIT_ERR;
                  rv = FAL0;
                  break;
               }
               dot = message + odot;
               pstate |= odid;
            }
         }

         _reset_on_stop = 1;
         exit_status = EXIT_OK;
      }

      /* Read a line of commands and handle end of file specially */
jreadline:
      ev.ev_line.l = ev.ev_line_size;
      n = readline_input(NULL, TRU1, &ev.ev_line.s, &ev.ev_line.l,
            ev.ev_new_content);
      ev.ev_line_size = (ui32_t)ev.ev_line.l;
      ev.ev_line.l = (ui32_t)n;
      _reset_on_stop = 0;
      if (n < 0) {
         /* EOF */
         if (pstate & PS_LOADING)
            break;
         if (pstate & PS_SOURCING) {
            unstack();
            continue;
         }
         if ((options & OPT_INTERACTIVE) && ok_blook(ignoreeof)) {
            printf(_("*ignoreeof* set, use `quit' to quit.\n"));
            continue;
         }
         break;
      }

      temporary_orig_line = ((pstate & PS_SOURCING) ||
         !(options & OPT_INTERACTIVE)) ? NULL
          : savestrbuf(ev.ev_line.s, ev.ev_line.l);
      pstate &= ~PS_HOOK_MASK;
      if (evaluate(&ev)) {
         if (pstate & PS_LOADING) /* TODO mess; join PS_EVAL_ERROR.. */
            rv = FAL0;
         break;
      }
      if ((options & OPT_BATCH_FLAG) && ok_blook(batch_exit_on_error)) {
         if (exit_status != EXIT_OK)
            break;
         if ((pstate & (PS_IN_LOAD | PS_EVAL_ERROR)) == PS_EVAL_ERROR) {
            exit_status = EXIT_ERR;
            break;
         }
      }
      if (!(pstate & PS_SOURCING) && (options & OPT_INTERACTIVE)) {
         if (ev.ev_new_content != NULL)
            goto jreadline;
         /* That *can* happen since evaluate() unstack()s on error! */
         if (temporary_orig_line != NULL)
            n_tty_addhist(temporary_orig_line, !ev.ev_add_history);
      }
   }

   if (ev.ev_line.s != NULL)
      free(ev.ev_line.s);
   if (pstate & PS_SOURCING)
      sreset(FAL0);
   NYD_LEAVE;
   return rv;
}

FL int
execute(char *linebuf, size_t linesize) /* XXX LEGACY */
{
   struct eval_ctx ev;
   int rv;
   NYD_ENTER;

   memset(&ev, 0, sizeof ev);
   ev.ev_line.s = linebuf;
   ev.ev_line.l = linesize;
   ev.ev_is_recursive = TRU1;
   n_COLOUR( n_colour_env_push(); )
   rv = evaluate(&ev);
   n_COLOUR( n_colour_env_pop(FAL0); )

   NYD_LEAVE;
   return rv;
}

FL int
evaluate(struct eval_ctx *evp)
{
   struct str line;
   char _wordbuf[2], *arglist[MAXARGC], *cp, *word;
   struct cmd_ghost *cg = NULL;
   struct cmd const *com = NULL;
   int muvec[2], c, e = 1;
   NYD_ENTER;

   line = evp->ev_line; /* XXX don't change original (buffer pointer) */
   evp->ev_add_history = FAL0;
   evp->ev_new_content = NULL;

   /* Command ghosts that refer to shell commands or macro expansion restart */
jrestart:

   /* Strip the white space away from the beginning of the command */
   for (cp = line.s; whitechar(*cp); ++cp)
      ;
   line.l -= PTR2SIZE(cp - line.s);

   /* Ignore comments */
   if (*cp == '#')
      goto jleave0;

   /* Handle ! differently to get the correct lexical conventions */
   if (*cp == '!') {
      if (pstate & PS_SOURCING) {
         n_err(_("Can't `!' while `source'ing\n"));
         goto jleave;
      }
      c_shell(++cp);
      evp->ev_add_history = TRU1;
      goto jleave0;
   }

   /* Isolate the actual command; since it may not necessarily be
    * separated from the arguments (as in `p1') we need to duplicate it to
    * be able to create a NUL terminated version.
    * We must be aware of several special one letter commands here */
   arglist[0] = cp;
   if ((cp = _lex_isolate(cp)) == arglist[0] &&
         (*cp == '|' || *cp == '~' || *cp == '?'))
      ++cp;
   c = (int)PTR2SIZE(cp - arglist[0]);
   line.l -= c;
   word = UICMP(z, c, <, sizeof _wordbuf) ? _wordbuf : salloc(c +1);
   memcpy(word, arglist[0], c);
   word[c] = '\0';

   /* Look up the command; if not found, bitch.
    * Normally, a blank command would map to the first command in the
    * table; while PS_SOURCING, however, we ignore blank lines to eliminate
    * confusion; act just the same for ghosts */
   if (*word == '\0') {
      if ((pstate & PS_SOURCING) || cg != NULL)
         goto jleave0;
      com = _cmd_tab + 0;
      goto jexec;
   }

   /* If this is the first evaluation, check command ghosts */
   if (cg == NULL) {
      /* TODO relink list head, so it's sorted on usage over time?
       * TODO in fact, there should be one hashmap over all commands and ghosts
       * TODO so that the lookup could be made much more efficient than it is
       * TODO now (two adjacent list searches! */
      for (cg = _cmd_ghosts; cg != NULL; cg = cg->next)
         if (!strcmp(word, cg->name)) {
            if (line.l > 0) {
               size_t i = cg->cmd.l;
               line.s = salloc(i + line.l +1);
               memcpy(line.s, cg->cmd.s, i);
               memcpy(line.s + i, cp, line.l);
               line.s[i += line.l] = '\0';
               line.l = i;
            } else {
               line.s = cg->cmd.s;
               line.l = cg->cmd.l;
            }
            goto jrestart;
         }
   }

   if ((com = _lex(word)) == NULL || com->func == &c_cmdnotsupp) {
      bool_t s = condstack_isskip();
      if (!s || (options & OPT_D_V))
         n_err(_("Unknown command%s: `%s'\n"),
            (s ? _(" (conditionally ignored)") : ""), word);
      if (s)
         goto jleave0;
      if (com != NULL) {
         c_cmdnotsupp(NULL);
         com = NULL;
      }
      goto jleave;
   }

   /* See if we should execute the command -- if a conditional we always
    * execute it, otherwise, check the state of cond */
jexec:
   if (!(com->argtype & ARG_F) && condstack_isskip())
      goto jleave0;

   /* Process the arguments to the command, depending on the type it expects,
    * default to error.  If we're PS_SOURCING an interactive command: error */
   if ((options & OPT_SENDMODE) && !(com->argtype & ARG_M)) {
      n_err(_("May not execute `%s' while sending\n"), com->name);
      goto jleave;
   }
   if ((pstate & PS_SOURCING) && (com->argtype & ARG_I)) {
      n_err(_("May not execute `%s' while `source'ing\n"), com->name);
      goto jleave;
   }
   if (!(mb.mb_perm & MB_DELE) && (com->argtype & ARG_W)) {
      n_err(_("May not execute `%s' -- message file is read only\n"),
         com->name);
      goto jleave;
   }
   if (evp->ev_is_recursive && (com->argtype & ARG_R)) {
      n_err(_("Cannot recursively invoke `%s'\n"), com->name);
      goto jleave;
   }
   if (mb.mb_type == MB_VOID && (com->argtype & ARG_A)) {
      n_err(_("Cannot execute `%s' without active mailbox\n"), com->name);
      goto jleave;
   }
   if (com->argtype & ARG_O)
      OBSOLETE2(_("this command will be removed"), com->name);

   if (com->argtype & ARG_V)
      temporary_arg_v_store = NULL;

   switch (com->argtype & ARG_ARGMASK) {
   case ARG_MSGLIST:
      /* Message list defaulting to nearest forward legal message */
      if (n_msgvec == NULL)
         goto je96;
      if ((c = getmsglist(cp, n_msgvec, com->msgflag)) < 0)
         break;
      if (c == 0) {
         *n_msgvec = first(com->msgflag, com->msgmask);
         if (*n_msgvec != 0)
            n_msgvec[1] = 0;
      }
      if (*n_msgvec == 0) {
         if (!(pstate & PS_HOOK_MASK))
            printf(_("No applicable messages\n"));
         break;
      }
      e = (*com->func)(n_msgvec);
      break;

   case ARG_NDMLIST:
      /* Message list with no defaults, but no error if none exist */
      if (n_msgvec == NULL) {
je96:
         n_err(_("Invalid use of \"message list\"\n"));
         break;
      }
      if ((c = getmsglist(cp, n_msgvec, com->msgflag)) < 0)
         break;
      e = (*com->func)(n_msgvec);
      break;

   case ARG_STRLIST:
      /* Just the straight string, with leading blanks removed */
      while (whitechar(*cp))
         ++cp;
      e = (*com->func)(cp);
      break;

   case ARG_RAWLIST:
   case ARG_ECHOLIST:
      /* A vector of strings, in shell style */
      if ((c = getrawlist(cp, line.l, arglist, NELEM(arglist),
            ((com->argtype & ARG_ARGMASK) == ARG_ECHOLIST))) < 0)
         break;
      if (c < com->minargs) {
         n_err(_("`%s' requires at least %d arg(s)\n"),
            com->name, com->minargs);
         break;
      }
      if (c > com->maxargs) {
         n_err(_("`%s' takes no more than %d arg(s)\n"),
            com->name, com->maxargs);
         break;
      }
      e = (*com->func)(arglist);
      break;

   case ARG_NOLIST:
      /* Just the constant zero, for exiting, eg. */
      e = (*com->func)(0);
      break;

   default:
      n_panic(_("Unknown argument type"));
   }

   if (e == 0 && (com->argtype & ARG_V) &&
         (cp = temporary_arg_v_store) != NULL) {
      temporary_arg_v_store = NULL;
      evp->ev_new_content = cp;
      goto jleave0;
   }
   if (!(com->argtype & ARG_H) && !(pstate & PS_MSGLIST_SAW_NO))
      evp->ev_add_history = TRU1;

jleave:
   /* Exit the current source file on error TODO what a mess! */
   if (e == 0)
      pstate &= ~PS_EVAL_ERROR;
   else {
      pstate |= PS_EVAL_ERROR;
      if (e < 0 || (pstate & PS_LOADING)) {
         e = 1;
         goto jret;
      }
      if (pstate & PS_SOURCING)
         unstack();
      goto jret0;
   }
   if (com == NULL)
      goto jret0;
   if ((com->argtype & ARG_P) && ok_blook(autoprint))
      if (visible(dot)) {
         muvec[0] = (int)PTR2SIZE(dot - message + 1);
         muvec[1] = 0;
         c_type(muvec); /* TODO what if error?  re-eval! */
      }
   if (!(pstate & (PS_SOURCING | PS_HOOK_MASK)) && !(com->argtype & ARG_T))
      pstate |= PS_SAW_COMMAND;
jleave0:
   pstate &= ~PS_EVAL_ERROR;
jret0:
   e = 0;
jret:
   NYD_LEAVE;
   return e;
}

FL void
onintr(int s)
{
   NYD_X; /* Signal handler */

   if (handlerstacktop != NULL) {
      handlerstacktop(s);
      return;
   }
   safe_signal(SIGINT, onintr);
   noreset = 0;
   while (pstate & PS_SOURCING)
      unstack();

   termios_state_reset();
   close_all_files();

   if (image >= 0) {
      close(image);
      image = -1;
   }
   if (interrupts != 1)
      n_err_sighdl(_("Interrupt\n"));
   safe_signal(SIGPIPE, _oldpipe);
   reset(0);
}

#ifdef HAVE_DOCSTRINGS
FL bool_t
print_comm_docstr(char const *comm)
{
   struct cmd_ghost const *cg;
   struct cmd const *cp, *cpmax;
   bool_t rv = FAL0;
   NYD_ENTER;

   /* Ghosts take precedence */
   for (cg = _cmd_ghosts; cg != NULL; cg = cg->next)
      if (!strcmp(comm, cg->name)) {
         printf("%s -> ", comm);
         comm = cg->cmd.s;
         break;
      }

   cpmax = (cp = _cmd_tab) + NELEM(_cmd_tab);
jredo:
   for (; cp < cpmax; ++cp) {
      if (!strcmp(comm, cp->name))
         printf("%s: %s\n", comm, V_(cp->doc));
      else if (is_prefix(comm, cp->name))
         printf("%s (%s): %s\n", comm, cp->name, V_(cp->doc));
      else
         continue;
      rv = TRU1;
      break;
   }
   if (!rv && cpmax == _cmd_tab + NELEM(_cmd_tab)) {
      cpmax = (cp = _special_cmd_tab) + NELEM(_special_cmd_tab);
      goto jredo;
   }

   if (!rv && cg != NULL) {
      printf("\"%s\"\n", comm);
      rv = TRU1;
   }
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_DOCSTRINGS */

/* s-it-mode */
