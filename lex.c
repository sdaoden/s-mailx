/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ (Lexical processing of) Commands, and the (blocking) "event mainloop".
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

#include <fcntl.h>

struct cmd {
   char const     *name;         /* Name of command */
   int            (*func)(void*); /* Implementor of command */
   enum argtype   argtype;       /* Arglist type (see below) */
   short          msgflag;       /* Required flags of msgs */
   short          msgmask;       /* Relevant flags of msgs */
#ifdef HAVE_DOCSTRINGS
   int            docid;         /* Translation id of .doc */
   char const     *doc;          /* One line doc for command */
#endif
};
/* Yechh, can't initialize unions */
#define minargs   msgflag        /* Minimum argcount for RAWLIST */
#define maxargs   msgmask        /* Max argcount for RAWLIST */

struct cmd_ghost {
   struct cmd_ghost  *next;
   struct str        cmd;        /* Data follows after .name */
   char              name[VFIELD_SIZE(sizeof(size_t))];
};

static int              *_msgvec;
static int              _reset_on_stop;   /* do a reset() if stopped */
static sighandler_type  _oldpipe;
static struct cmd_ghost *_cmd_ghosts;
/* _cmd_tab[] after fun protos */
static int              _lex_inithdr;     /* am printing startup headers */

/* Update mailname (if name != NULL) and displayname, return wether displayname
 * was large enough to swallow mailname */
static bool_t  _update_mailname(char const *name);
#ifdef HAVE_C90AMEND1 /* TODO unite __narrow_{pre,suf}fix() into one fun! */
SINLINE size_t __narrow_prefix(char const *cp, size_t maxl);
SINLINE size_t __narrow_suffix(char const *cp, size_t cpl, size_t maxl);
#endif

/* Isolate the command from the arguments */
static char *  _lex_isolate(char const *comm);

/* Get first-fit, or NULL */
static struct cmd const * _lex(char const *comm);

/* Command ghost handling */
static int     _ghost(void *v);
static int     _unghost(void *v);

/* Print a list of all commands */
static int     _pcmdlist(void *v);
static int     __pcmd_cmp(void const *s1, void const *s2);

/* Print the binaries compiled-in features */
static int     _features(void *v);

/* Print the binaries version number */
static int     _version(void *v);

/* When we wake up after ^Z, reprint the prompt */
static void    stop(int s);

/* Branch here on hangup signal and simulate "exit" */
static void    hangup(int s);

/* List of all commands */
static struct cmd const _cmd_tab[] = {
#include "cmd_tab.h"
};

#ifdef HAVE_C90AMEND1
SINLINE size_t
__narrow_prefix(char const *cp, size_t maxl)
{
   int err;
   size_t i, ok;
   NYD_ENTER;

   for (err = ok = i = 0; i < maxl;) {
      int ml = mblen(cp, maxl - i);
      if (ml < 0) { /* XXX _narrow_prefix(): mblen() error; action? */
         (void)mblen(NULL, 0);
         err = 1;
         ml = 1;
      } else {
         if (!err)
            ok = i;
         err = 0;
         if (ml == 0)
            break;
      }
      cp += ml;
      i += ml;
   }
   NYD_LEAVE;
   return ok;
}

SINLINE size_t
__narrow_suffix(char const *cp, size_t cpl, size_t maxl)
{
   int err;
   size_t i, ok;
   NYD_ENTER;

   for (err = ok = i = 0; cpl > maxl || err;) {
      int ml = mblen(cp, cpl);
      if (ml < 0) { /* XXX _narrow_suffix(): mblen() error; action? */
         (void)mblen(NULL, 0);
         err = 1;
         ml = 1;
      } else {
         if (!err)
            ok = i;
         err = 0;
         if (ml == 0)
            break;
      }
      cp += ml;
      i += ml;
      cpl -= ml;
   }
   NYD_LEAVE;
   return ok;
}
#endif /* HAVE_C90AMEND1 */

static bool_t
_update_mailname(char const *name)
{
   char tbuf[PATH_MAX], *mailp, *dispp;
   size_t i, j;
   bool_t rv = TRU1;
   NYD_ENTER;

   /* Don't realpath(3) if it's only an update request */
   if (name != NULL) {
#ifdef HAVE_REALPATH
      enum protocol p = which_protocol(name);
      if (p == PROTO_FILE || p == PROTO_MAILDIR) {
         if (realpath(name, mailname) == NULL) {
            fprintf(stderr, tr(151, "Can't canonicalize `%s'\n"), name);
            rv = FAL0;
            goto jdocopy;
         }
      } else
jdocopy:
#endif
         n_strlcpy(mailname, name, sizeof(mailname));
   }

   mailp = mailname;
   dispp = displayname;

   /* Don't display an absolute path but "+FOLDER" if under *folder* */
   if (getfold(tbuf, sizeof tbuf)) {
      i = strlen(tbuf);
      if (i < sizeof(tbuf) -1)
         tbuf[i++] = '/';
      if (!strncmp(tbuf, mailp, i)) {
         mailp += i;
         *dispp++ = '+';
      }
   }

   /* We want to see the name of the folder .. on the screen */
   i = strlen(mailp);
   if (i < sizeof(displayname) -1)
      memcpy(dispp, mailp, i +1);
   else {
      rv = FAL0;
      /* Avoid disrupting multibyte sequences (if possible) */
#ifndef HAVE_C90AMEND1
      j = sizeof(displayname) / 3 - 1;
      i -= sizeof(displayname) - (1/* + */ + 3) - j;
#else
      j = __narrow_prefix(mailp, sizeof(displayname) / 3);
      i = j + __narrow_suffix(mailp + j, i - j,
         sizeof(displayname) - (1/* + */ + 3 + 1) - j);
#endif
      snprintf(dispp, sizeof(displayname), "%.*s...%s",
         (int)j, mailp, mailp + i);
   }
   NYD_LEAVE;
   return rv;
}

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
   struct cmd const *cp;
   NYD_ENTER;

   for (cp = _cmd_tab; cp->name != NULL; ++cp)
      if (*comm == *cp->name && is_prefix(comm, cp->name))
         goto jleave;
   cp = NULL;
jleave:
   NYD_LEAVE;
   return cp;
}

static int
_ghost(void *v)
{
   char const **argv = v;
   struct cmd_ghost *lcg, *cg;
   size_t nl, cl;
   NYD_ENTER;

   /* Show the list? */
   if (*argv == NULL) {
      printf(tr(144, "Command ghosts are:\n"));
      for (nl = 0, cg = _cmd_ghosts; cg != NULL; cg = cg->next) {
         cl = strlen(cg->name) + 5 + cg->cmd.l + 3;
         nl += cl;
         if (UICMP(z, nl, >=, scrnwidth)) {
            nl = cl;
            printf("\n");
         }
         printf((cg->next != NULL ? "%s -> <%s>, " : "%s -> <%s>\n"),
            cg->name, cg->cmd.s);
      }
      v = NULL;
      goto jleave;
   }

   /* Request to add new ghost */
   if (argv[1] == NULL || argv[1][0] == '\0' || argv[2] != NULL) {
      fprintf(stderr, tr(159, "Usage: %s\n"),
         tr(425, "Define a <ghost> of <command>, or list all ghosts"));
      v = NULL;
      goto jleave;
   }

   /* Check that we can deal with this one */
   if (argv[0] == _lex_isolate(argv[0])) {
      fprintf(stderr, tr(151, "Can't canonicalize `%s'\n"), argv[0]);
      v = NULL;
      goto jleave;
   }

   /* Always recreate */
   for (lcg = NULL, cg = _cmd_ghosts; cg != NULL; lcg = cg, cg = cg->next)
      if (!strcmp(cg->name, argv[0])) {
         if (lcg != NULL)
            lcg->next = cg->next;
         else
            _cmd_ghosts = cg->next;
         free(cg);
         break;
      }

   /* Need a new one */
   nl = strlen(argv[0]) +1;
   cl = strlen(argv[1]) +1;
   cg = smalloc(sizeof(*cg) - VFIELD_SIZEOF(struct cmd_ghost, name) + nl + cl);
   cg->next = _cmd_ghosts;
   memcpy(cg->name, argv[0], nl);
   cg->cmd.s = cg->name + nl;
   cg->cmd.l = cl -1;
   memcpy(cg->cmd.s, argv[1], cl);

   _cmd_ghosts = cg;
jleave:
   NYD_LEAVE;
   return (v == NULL);
}

static int
_unghost(void *v)
{
   int rv = 0;
   char const **argv = v, *cp;
   struct cmd_ghost *lcg, *cg;
   NYD_ENTER;

   while ((cp = *argv++) != NULL) {
      for (lcg = NULL, cg = _cmd_ghosts; cg != NULL; lcg = cg, cg = cg->next)
         if (!strcmp(cg->name, cp)) {
            if (lcg != NULL)
               lcg->next = cg->next;
            else
               _cmd_ghosts = cg->next;
            free(cg);
            goto jouter;
         }
      fprintf(stderr, tr(91, "Unknown command: `%s'\n"), cp);
      rv = 1;
jouter:
      ;
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
_pcmdlist(void *v)
{
   struct cmd const **cpa, *cp, **cursor;
   size_t i;
   NYD_ENTER;
   UNUSED(v);

   for (i = 0; _cmd_tab[i].name != NULL; ++i)
      ;
   ++i;
   cpa = ac_alloc(sizeof(cp) * i);

   for (i = 0; (cp = _cmd_tab + i)->name != NULL; ++i)
      cpa[i] = cp;
   cpa[i] = NULL;

   qsort(cpa, i, sizeof(cp), &__pcmd_cmp);

   printf(tr(14, "Commands are:\n"));
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
_features(void *v)
{
   NYD_ENTER;
   UNUSED(v);
   printf(tr(523, "Features: %s\n"), features);
   NYD_LEAVE;
   return 0;
}

static int
_version(void *v)
{
   NYD_ENTER;
   UNUSED(v);
   printf(tr(111, "Version %s\n"), version);
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
   kill(0, s);
   sigprocmask(SIG_BLOCK, &nset, NULL);
   safe_signal(s, old_action);
   if (_reset_on_stop) {
      _reset_on_stop = 0;
      reset(0);
   }
}

static void
hangup(int s)
{
   NYD_X; /* Signal handler */
   UNUSED(s);
   /* nothing to do? */
   exit(1);
}

FL int
setfile(char const *name, int nmail) /* TODO oh my god */
{
   static int shudclob;

   struct stat stb;
   struct flock flp;
   FILE *ibuf = NULL;
   int rv, i, compressed = 0, omsgCount = 0;
   bool_t isedit;
   char const *who;
   size_t offset;
   struct shortcut *sh;
   NYD_ENTER;

   /* Note we don't 'userid(myname) != getuid()', preliminary steps are usually
    * necessary to make a mailbox accessible by a different user, and if that
    * has happened, let's just let the usual file perms decide */
   who = (name[1] != '\0') ? name + 1 : myname;
   isedit = (*name != '%' && ((sh = get_shortcut(name)) == NULL ||
         *sh->sh_long != '%'));
   if ((name = expand(name)) == NULL)
      goto jem1;

   switch (which_protocol(name)) {
   case PROTO_FILE:
      break;
   case PROTO_MAILDIR:
      rv = maildir_setfile(name, nmail, isedit);
      goto jleave;
#ifdef HAVE_POP3
   case PROTO_POP3:
      shudclob = 1;
      rv = pop3_setfile(name, nmail, isedit);
      goto jleave;
#endif
#ifdef HAVE_IMAP
   case PROTO_IMAP:
      shudclob = 1;
      if (nmail && mb.mb_type == MB_CACHE)
         rv = 1;
      else
         rv = imap_setfile(name, nmail, isedit);
      goto jleave;
#endif
   default:
      fprintf(stderr, tr(217, "Cannot handle protocol: %s\n"), name);
      goto jem1;
   }

   /* FIXME this FILE leaks if quit()->edstop() reset()s!  This entire code
    * FIXME here is total crap, below we open(2) the same name again just to
    * FIXME close it right away etc.  The normal thing would be to (1) finalize
    * FIXME the current box and (2) open the new box; yet, since (2) may fail
    * FIXME we terribly need our VOID box to make this logic order possible! */
   if ((ibuf = Zopen(name, "r", &compressed)) == NULL) {
      if ((!isedit && errno == ENOENT) || nmail) {
         if (nmail)
            goto jnonmail;
         goto jnomail;
      }
      perror(name);
      goto jem1;
   }

   if (fstat(fileno(ibuf), &stb) == -1) {
      if (nmail)
         goto jnonmail;
      perror("fstat");
      goto jem1;
   }

   if (S_ISREG(stb.st_mode) ||
         ((options & OPT_BATCH_FLAG) && !strcmp(name, "/dev/null"))) {
      /* EMPTY */
   } else {
      if (nmail)
         goto jnonmail;
      errno = S_ISDIR(stb.st_mode) ? EISDIR : EINVAL;
      perror(name);
      goto jem1;
   }

   /* Looks like all will be well.  We must now relinquish our hold on the
    * current set of stuff.  Must hold signals while we are reading the new
    * file, else we will ruin the message[] data structure */

   hold_sigs(); /* TODO note on this one in quit.c:quit() */
   if (shudclob && !nmail)
      quit();
#ifdef HAVE_SOCKETS
   if (!nmail && mb.mb_sock.s_fd >= 0)
      sclose(&mb.mb_sock); /* TODO sorry? VMAILFS->close(), thank you */
#endif

   /* Copy the messages into /tmp and set pointers */
   flp.l_type = F_RDLCK;
   flp.l_start = 0;
   flp.l_whence = SEEK_SET;
   if (!nmail) {
      mb.mb_type = MB_FILE;
      mb.mb_perm = (options & OPT_R_FLAG) ? 0 : MB_DELE | MB_EDIT;
      mb.mb_compressed = compressed;
      if (compressed) {
         if (compressed & 0200)
            mb.mb_perm = 0;
      } else {
         if ((i = open(name, O_WRONLY)) == -1)
            mb.mb_perm = 0;
         else
            close(i);
      }
      if (shudclob) {
         if (mb.mb_itf) {
            fclose(mb.mb_itf);
            mb.mb_itf = NULL;
         }
         if (mb.mb_otf) {
            fclose(mb.mb_otf);
            mb.mb_otf = NULL;
         }
      }
      shudclob = 1;
      edit = isedit;
      initbox(name);
      offset = 0;
      flp.l_len = 0;
      if (!edit && fcntl(fileno(ibuf), F_SETLKW, &flp) == -1) {/*TODO dotlock!*/
         perror("Unable to lock mailbox");
         rele_sigs();
         goto jem1;
      }
   } else /* nmail */{
      fseek(mb.mb_otf, 0L, SEEK_END);
      fseek(ibuf, mailsize, SEEK_SET);
      offset = mailsize;
      omsgCount = msgCount;
      flp.l_len = offset;
      if (!edit && fcntl(fileno(ibuf), F_SETLKW, &flp) == -1) {/*TODO dotlock!*/
         rele_sigs();
         goto jnonmail;
      }
   }
   mailsize = fsize(ibuf);

   if (nmail && UICMP(z, mailsize, <=, offset)) {
      rele_sigs();
      goto jnonmail;
   }
   setptr(ibuf, offset);
   setmsize(msgCount);
   if (nmail && mb.mb_sorted) {
      mb.mb_threaded = 0;
      c_sort((void*)-1);
   }

   Fclose(ibuf);
   ibuf = NULL;
   rele_sigs();
   if (!nmail)
      sawcom = FAL0;

   if ((!edit || nmail) && msgCount == 0) {
jnonmail:
      if (!nmail) {
         if (!ok_blook(emptystart))
jnomail:
            fprintf(stderr, tr(88, "No mail for %s\n"), who);
      }
      rv = 1;
      goto jleave;
   }
   if (nmail)
      newmailinfo(omsgCount);

   rv = 0;
jleave:
   if (ibuf != NULL)
      Fclose(ibuf);
   NYD_LEAVE;
   return rv;
jem1:
   rv = -1;
   goto jleave;
}

FL int
newmailinfo(int omsgCount)
{
   int mdot, i;
   NYD_ENTER;

   for (i = 0; i < omsgCount; ++i)
      message[i].m_flag &= ~MNEWEST;
   if (msgCount > omsgCount) {
      for (i = omsgCount; i < msgCount; ++i)
         message[i].m_flag |= MNEWEST;
      printf(tr(158, "New mail has arrived.\n"));
      if ((i = msgCount - omsgCount) == 1)
         printf(tr(214, "Loaded 1 new message.\n"));
      else
         printf(tr(215, "Loaded %d new messages.\n"), i);
   } else
      printf(tr(224, "Loaded %d messages.\n"), msgCount);
   callhook(mailname, 1);
   mdot = getmdot(1);
   if (ok_blook(header))
      print_headers(omsgCount + 1, msgCount, FAL0);
   NYD_LEAVE;
   return mdot;
}

FL void
commands(void)
{
   struct eval_ctx ev;
   int n;
   NYD_ENTER;

   if (!sourcing) {
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
      interrupts = 0;
      handlerstacktop = NULL;

      if (!sourcing && (options & OPT_INTERACTIVE)) {
         char *cp;

         if ((cp = ok_vlook(newmail)) == NULL)
            cp = ok_vlook(autoinc); /* TODO legacy */
         if ((options & OPT_TTYIN) && (cp != NULL || mb.mb_type == MB_IMAP)) {
            struct stat st;

            n = (cp != NULL && strcmp(cp, "noimap") && strcmp(cp, "nopoll"));
            if ((mb.mb_type == MB_FILE && !stat(mailname, &st) &&
                     st.st_size > mailsize) ||
#ifdef HAVE_IMAP
                  (mb.mb_type == MB_IMAP && imap_newmail(n) > (cp == NULL)) ||
#endif
                  (mb.mb_type == MB_MAILDIR && n != 0)) {
               size_t odot = PTR2SIZE(dot - message);
               bool_t odid = did_print_dot;

               setfile(mailname, 1);
               if (mb.mb_type != MB_IMAP) {
                  dot = message + odot;
                  did_print_dot = odid;
               }
            }
         }

         _reset_on_stop = 1;
         exit_status = EXIT_OK;
      }

#ifdef HAVE_COLOUR
      colour_table = NULL; /* XXX intermediate hack */
#endif
      if (temporary_localopts_store != NULL) /* XXX intermediate hack */
         temporary_localopts_free(); /* XXX intermediate hack */
      sreset(sourcing);
      if (!sourcing) {
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
            ev.ev_line.l = 0;
         }
      }

      /* Read a line of commands and handle end of file specially */
jreadline:
      n = readline_input(NULL, TRU1, &ev.ev_line.s, &ev.ev_line.l,
            ev.ev_new_content);
      _reset_on_stop = 0;
      if (n < 0) {
         /* EOF */
         if (loading)
            break;
         if (sourcing) {
            unstack();
            continue;
         }
         if ((options & OPT_INTERACTIVE) && ok_blook(ignoreeof)) {
            printf(tr(89, "Use `quit' to quit.\n"));
            continue;
         }
         break;
      }

      inhook = 0;
      if (evaluate(&ev))
         break;
      if ((options & OPT_BATCH_FLAG) && ok_blook(batch_exit_on_error)) {
         if (exit_status != EXIT_OK)
            break;
         /* TODO *batch-exit-on-error*: sourcing and loading MUST BE FLAGS!
          * TODO the current behaviour is suboptimal AT BEST! */
         if (exec_last_comm_error != 0 && !sourcing && !loading) {
            exit_status = EXIT_ERR;
            break;
         }
      }
      if (!sourcing && (options & OPT_INTERACTIVE)) {
         if (ev.ev_new_content != NULL)
            goto jreadline;
         if (ev.ev_add_history)
            tty_addhist(ev.ev_line.s);
      }
   }

   if (ev.ev_line.s != NULL)
      free(ev.ev_line.s);
   if (sourcing)
      sreset(FAL0);
   NYD_LEAVE;
}

FL int
execute(char *linebuf, int contxt, size_t linesize) /* XXX LEGACY */
{
   struct eval_ctx ev;
#ifdef HAVE_COLOUR
   struct colour_table *ct_save;
#endif
   int rv;
   NYD_ENTER;

   /* TODO Maybe recursion from within collect.c!  As long as we don't have
    * TODO a value carrier that transports the entire state of a recursion
    * TODO we need to save away also the colour table */
#ifdef HAVE_COLOUR
   ct_save = colour_table;
   colour_table = NULL;
#endif

   memset(&ev, 0, sizeof ev);
   ev.ev_line.s = linebuf;
   ev.ev_line.l = linesize;
   ev.ev_is_recursive = (contxt != 0);
   rv = evaluate(&ev);

#ifdef HAVE_COLOUR
   colour_table = ct_save;
#endif
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
      if (sourcing) {
         fprintf(stderr, tr(90, "Can't `!' while sourcing\n"));
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
    * table; while sourcing, however, we ignore blank lines to eliminate
    * confusion; act just the same for ghosts */
   if (*word == '\0') {
      if (sourcing || cg != NULL)
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
               line.s = salloc(i + 1 + line.l +1);
               memcpy(line.s, cg->cmd.s, i);
               line.s[i++] = ' ';
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
      fprintf(stderr, tr(91, "Unknown command: `%s'\n"), word);
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
    * default to error.  If we're sourcing an interactive command: error */
   if ((options & OPT_SENDMODE) && !(com->argtype & ARG_M)) {
      fprintf(stderr, tr(92, "May not execute `%s' while sending\n"),
         com->name);
      goto jleave;
   }
   if (sourcing && (com->argtype & ARG_I)) {
      fprintf(stderr, tr(93, "May not execute `%s' while sourcing\n"),
         com->name);
      goto jleave;
   }
   if (!(mb.mb_perm & MB_DELE) && (com->argtype & ARG_W)) {
      fprintf(stderr, tr(94, "May not execute `%s' -- "
         "message file is read only\n"), com->name);
      goto jleave;
   }
   if (evp->ev_is_recursive && (com->argtype & ARG_R)) {
      fprintf(stderr, tr(95, "Cannot recursively invoke `%s'\n"), com->name);
      goto jleave;
   }
   if (mb.mb_type == MB_VOID && (com->argtype & ARG_A)) {
      fprintf(stderr, tr(257, "Cannot execute `%s' without active mailbox\n"),
         com->name);
      goto jleave;
   }

   if (com->argtype & ARG_V)
      temporary_arg_v_store = NULL;

   switch (com->argtype & ARG_ARGMASK) {
   case ARG_MSGLIST:
      /* Message list defaulting to nearest forward legal message */
      if (_msgvec == NULL)
         goto je96;
      if ((c = getmsglist(cp, _msgvec, com->msgflag)) < 0)
         break;
      if (c == 0) {
         *_msgvec = first(com->msgflag, com->msgmask);
         if (*_msgvec != 0)
            _msgvec[1] = 0;
      }
      if (*_msgvec == 0) {
         if (!inhook)
            printf(tr(97, "No applicable messages\n"));
         break;
      }
      e = (*com->func)(_msgvec);
      break;

   case ARG_NDMLIST:
      /* Message list with no defaults, but no error if none exist */
      if (_msgvec == NULL) {
je96:
         fprintf(stderr, tr(96, "Illegal use of `message list'\n"));
         break;
      }
      if ((c = getmsglist(cp, _msgvec, com->msgflag)) < 0)
         break;
      e = (*com->func)(_msgvec);
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
         fprintf(stderr, tr(99, "`%s' requires at least %d arg(s)\n"),
            com->name, com->minargs);
         break;
      }
      if (c > com->maxargs) {
         fprintf(stderr, tr(100, "`%s' takes no more than %d arg(s)\n"),
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
      panic(tr(101, "Unknown argument type"));
   }

   if (e == 0 && (com->argtype & ARG_V) &&
         (cp = temporary_arg_v_store) != NULL) {
      temporary_arg_v_store = NULL;
      evp->ev_new_content = cp;
      goto jleave0;
   }
   if (!(com->argtype & ARG_H) && !list_saw_numbers)
      evp->ev_add_history = TRU1;

jleave:
   /* Exit the current source file on error */
   if ((exec_last_comm_error = (e != 0))) {
      if (e < 0 || loading) {
         e = 1;
         goto jret;
      }
      if (sourcing)
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
   if (!sourcing && !inhook && !(com->argtype & ARG_T))
      sawcom = TRU1;
jleave0:
   exec_last_comm_error = 0;
jret0:
   e = 0;
jret:
   NYD_LEAVE;
   return e;
}

FL void
setmsize(int sz)
{
   NYD_ENTER;
   if (_msgvec != NULL)
      free(_msgvec);
   _msgvec = scalloc(sz + 1, sizeof *_msgvec);
   NYD_LEAVE;
}

FL void
print_header_summary(char const *Larg)
{
   size_t bot, top, i, j;
   NYD_ENTER;

   if (Larg != NULL) {
      /* Avoid any messages XXX add a make_mua_silent() and use it? */
      if ((options & (OPT_VERBOSE | OPT_HEADERSONLY)) == OPT_HEADERSONLY) {
         freopen("/dev/null", "w", stdout);
         freopen("/dev/null", "w", stderr);
      }
      i = (getmsglist(/*TODO make arg const */UNCONST(Larg), _msgvec, 0) <= 0);
      if (options & OPT_HEADERSONLY) {
         exit_status = (int)i;
         goto jleave;
      }
      if (i)
         goto jleave;
      for (bot = msgCount, top = 0, i = 0; (j = _msgvec[i]) != 0; ++i) {
         if (bot > j)
            bot = j;
         if (top < j)
            top = j;
      }
   } else
      bot = 1, top = msgCount;
   print_headers(bot, top, (Larg != NULL)); /* TODO should take iterator!! */
jleave:
   NYD_LEAVE;
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
   if (!_lex_inithdr)
      sawcom = TRU1;
   _lex_inithdr = 0;
   while (sourcing)
      unstack();

   termios_state_reset();
   close_all_files();

   if (image >= 0) {
      close(image);
      image = -1;
   }
   if (interrupts != 1)
      fprintf(stderr, tr(102, "Interrupt\n"));
   safe_signal(SIGPIPE, _oldpipe);
   reset(0);
}

FL void
announce(int printheaders)
{
   int vec[2], mdot;
   NYD_ENTER;

   mdot = newfileinfo();
   vec[0] = mdot;
   vec[1] = 0;
   dot = message + mdot - 1;
   if (printheaders && msgCount > 0 && ok_blook(header)) {
      ++_lex_inithdr;
      c_headers(vec); /* XXX errors? */
      _lex_inithdr = 0;
   }
   NYD_LEAVE;
}

FL int
newfileinfo(void)
{
   struct message *mp;
   int u, n, mdot, d, s, hidden, moved;
   NYD_ENTER;

   if (mb.mb_type == MB_VOID) {
      mdot = 1;
      goto jleave;
   }

   mdot = getmdot(0);
   s = d = hidden = moved =0;
   for (mp = message, n = 0, u = 0; PTRCMP(mp, <, message + msgCount); ++mp) {
      if (mp->m_flag & MNEW)
         ++n;
      if ((mp->m_flag & MREAD) == 0)
         ++u;
      if ((mp->m_flag & (MDELETED | MSAVED)) == (MDELETED | MSAVED))
         ++moved;
      if ((mp->m_flag & (MDELETED | MSAVED)) == MDELETED)
         ++d;
      if ((mp->m_flag & (MDELETED | MSAVED)) == MSAVED)
         ++s;
      if (mp->m_flag & MHIDDEN)
         ++hidden;
   }

   /* If displayname gets truncated the user effectively has no option to see
    * the full pathname of the mailbox, so print it at least for '? fi' */
   printf(tr(103, "\"%s\": "),
      (_update_mailname(NULL) ? displayname : mailname));
   if (msgCount == 1)
      printf(tr(104, "1 message"));
   else
      printf(tr(105, "%d messages"), msgCount);
   if (n > 0)
      printf(tr(106, " %d new"), n);
   if (u-n > 0)
      printf(tr(107, " %d unread"), u);
   if (d > 0)
      printf(tr(108, " %d deleted"), d);
   if (s > 0)
      printf(tr(109, " %d saved"), s);
   if (moved > 0)
      printf(tr(136, " %d moved"), moved);
   if (hidden > 0)
      printf(tr(139, " %d hidden"), hidden);
   if (mb.mb_type == MB_CACHE)
      printf(" [Disconnected]");
   else if (mb.mb_perm == 0)
      printf(tr(110, " [Read only]"));
   printf("\n");
jleave:
   NYD_LEAVE;
   return mdot;
}

FL int
getmdot(int nmail)
{
   struct message *mp;
   char *cp;
   int mdot;
   enum mflag avoid = MHIDDEN | MDELETED;
   NYD_ENTER;

   if (!nmail) {
      if (ok_blook(autothread))
         c_thread(NULL);
      else if ((cp = ok_vlook(autosort)) != NULL) {
         free(mb.mb_sorted);
         mb.mb_sorted = sstrdup(cp);
         c_sort(NULL);
      }
   }
   if (mb.mb_type == MB_VOID) {
      mdot = 1;
      goto jleave;
   }

   if (nmail)
      for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
         if ((mp->m_flag & (MNEWEST | avoid)) == MNEWEST)
            break;

   if (!nmail || PTRCMP(mp, >=, message + msgCount)) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if ((mp->m_flag & (MNEW | avoid)) == MNEW)
               break;
      } else {
         for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
            if ((mp->m_flag & (MNEW | avoid)) == MNEW)
               break;
      }
   }

   if ((mb.mb_threaded ? (mp == NULL) : PTRCMP(mp, >=, message + msgCount))) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if (mp->m_flag & MFLAGGED)
               break;
      } else {
         for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
            if (mp->m_flag & MFLAGGED)
               break;
      }
   }

   if ((mb.mb_threaded ? (mp == NULL) : PTRCMP(mp, >=, message + msgCount))) {
      if (mb.mb_threaded) {
         for (mp = threadroot; mp != NULL; mp = next_in_thread(mp))
            if (!(mp->m_flag & (MREAD | avoid)))
               break;
      } else {
         for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
            if (!(mp->m_flag & (MREAD | avoid)))
               break;
      }
   }

   if ((mb.mb_threaded ? (mp != NULL) : PTRCMP(mp, <, message + msgCount)))
      mdot = (int)PTR2SIZE(mp - message + 1);
   else if (ok_blook(showlast)) {
      if (mb.mb_threaded) {
         for (mp = this_in_thread(threadroot, -1); mp;
               mp = prev_in_thread(mp))
            if (!(mp->m_flag & avoid))
               break;
         mdot = (mp != NULL) ? (int)PTR2SIZE(mp - message + 1) : msgCount;
      } else {
         for (mp = message + msgCount - 1; mp >= message; --mp)
            if (!(mp->m_flag & avoid))
               break;
         mdot = (mp >= message) ? (int)PTR2SIZE(mp - message + 1) : msgCount;
      }
   } else if (mb.mb_threaded) {
      for (mp = threadroot; mp; mp = next_in_thread(mp))
         if (!(mp->m_flag & avoid))
            break;
      mdot = (mp != NULL) ? (int)PTR2SIZE(mp - message + 1) : 1;
   } else {
      for (mp = message; PTRCMP(mp, <, message + msgCount); ++mp)
         if (!(mp->m_flag & avoid))
            break;
      mdot = PTRCMP(mp, <, message + msgCount)
            ? (int)PTR2SIZE(mp - message + 1) : 1;
   }
jleave:
   NYD_LEAVE;
   return mdot;
}

FL void
initbox(char const *name)
{
   char *tempMesg;
   NYD_ENTER;

   if (mb.mb_type != MB_VOID)
      n_strlcpy(prevfile, mailname, PATH_MAX);

   _update_mailname((name != mailname) ? name : NULL);

   if ((mb.mb_otf = Ftmp(&tempMesg, "tmpbox", OF_WRONLY | OF_HOLDSIGS, 0600)) ==
         NULL) {
      perror(tr(87, "temporary mail message file"));
      exit(1);
   }
   if ((mb.mb_itf = safe_fopen(tempMesg, "r", NULL)) == NULL) {
      perror(tr(87, "temporary mail message file"));
      exit(1);
   }
   Ftmp_release(&tempMesg);

   message_reset();
   mb.mb_threaded = 0;
   if (mb.mb_sorted != NULL) {
      free(mb.mb_sorted);
      mb.mb_sorted = NULL;
   }
#ifdef HAVE_IMAP
   mb.mb_flags = MB_NOFLAGS;
#endif
   prevdot = NULL;
   dot = NULL;
   did_print_dot = FAL0;
   NYD_LEAVE;
}

#ifdef HAVE_DOCSTRINGS
FL bool_t
print_comm_docstr(char const *comm)
{
   bool_t rv = FAL0;
   struct cmd_ghost *cg;
   struct cmd const *cp;
   NYD_ENTER;

   /* Ghosts take precedence */
   for (cg = _cmd_ghosts; cg != NULL; cg = cg->next)
      if (!strcmp(comm, cg->name)) {
         printf("%s -> <%s>\n", comm, cg->cmd.s);
         rv = TRU1;
         goto jleave;
      }

   for (cp = _cmd_tab; cp->name != NULL; ++cp) {
      if (cp->func == &c_cmdnotsupp)
         continue;
      if (!strcmp(comm, cp->name))
         printf("%s: %s\n", comm, tr(cp->docid, cp->doc));
      else if (is_prefix(comm, cp->name))
         printf("%s (%s): %s\n", comm, cp->name, tr(cp->docid, cp->doc));
      else
         continue;
      rv = TRU1;
      break;
   }
jleave:
   NYD_LEAVE;
   return rv;
}
#endif

/* vim:set fenc=utf-8:s-it-mode */
