/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Command input, lexing and evaluation, resource file loading and `source'ing.
 *@ TODO PS_ROBOT requires yet PS_SOURCING, which REALLY sucks.
 *@ TODO Commands and ghosts deserve a hashmap.  Or so.
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
#define n_FILE lex_input

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

enum a_lex_input_flags{
   a_LEX_NONE,
   a_LEX_FREE = 1<<0,            /* Structure was allocated, free() it */
   a_LEX_PIPE = 1<<1,            /* Open on a pipe */
   a_LEX_MACRO = 1<<2,           /* Running a macro */
   a_LEX_MACRO_FREE_DATA = 1<<3, /* Lines are allocated, free(3) once done */
   a_LEX_MACRO_X_OPTION = 1<<4,  /* Macro indeed command line -X option */
   a_LEX_MACRO_CMD = 1<<5,       /* Macro indeed single-line: ~:COMMAND */

   a_LEX_SUPER_MACRO = 1<<16     /* *Not* inheriting PS_SOURCING state */
};

struct a_lex_cmd{
   char const *lc_name;       /* Name of command */
   int (*lc_func)(void*);     /* Implementor of command */
   enum argtype lc_argtype;   /* Arglist type (see below) */
   si16_t lc_msgflag;         /* Required flags of msgs */
   si16_t lc_msgmask;         /* Relevant flags of msgs */
#ifdef HAVE_DOCSTRINGS
   char const *lc_doc;        /* One line doc for command */
#endif
};
/* Yechh, can't initialize unions */
#define lc_minargs lc_msgflag /* Minimum argcount for RAWLIST */
#define lc_maxargs lc_msgmask /* Max argcount for RAWLIST */

struct a_lex_ghost{
   struct a_lex_ghost *lg_next;
   struct str lg_cmd;            /* Data follows after .lg_name */
   char lg_name[n_VFIELD_SIZE(0)];
};

struct a_lex_eval_ctx{
   struct str le_line;           /* The terminated data to _evaluate() */
   ui32_t le_line_size;          /* May be used to store line memory size */
   bool_t le_is_recursive;       /* Evaluation in evaluation? (collect ~:) */
   ui8_t __dummy[3];
   bool_t le_add_history;        /* Add command to history (TRUM1=gabby)? */
   char const *le_new_content;   /* History: reenter line, start with this */
};

struct a_lex_input_stack{
   struct a_lex_input_stack *li_outer;
   FILE *li_file;                /* File we were in */
   void *li_cond;                /* Saved state of conditional stack */
   ui32_t li_flags;              /* enum a_lex_input_flags */
   ui32_t li_loff;               /* Pseudo (macro): index in .li_lines */
   char **li_lines;              /* Pseudo content, lines unfolded */
   char li_autorecmem[n_MEMORY_AUTOREC_TYPE_SIZEOF];
   char li_name[n_VFIELD_SIZE(0)]; /* Name of file or macro */
};
n_CTA(n_MEMORY_AUTOREC_TYPE_SIZEOF % sizeof(void*) == 0,
   "Inacceptible size of structure buffer");

static sighandler_type a_lex_oldpipe;
static struct a_lex_ghost *a_lex_ghosts;
/* a_lex_cmd_tab[] after fun protos */

/* */
static struct a_lex_input_stack *a_lex_input;

/* Isolate the command from the arguments */
static char *a_lex_isolate(char const *comm);

/* Command ghost handling */
static int a_lex_c_ghost(void *v);
static int a_lex_c_unghost(void *v);

/* Print a list of all commands */
static int a_lex_c_list(void *v);

static int a_lex__pcmd_cmp(void const *s1, void const *s2);

/* `help' / `?' command */
static int a_lex_c_help(void *v);

/* `quit' command */
static int a_lex_c_quit(void *v);

/* Print the binaries version number */
static int a_lex_c_version(void *v);

static int a_lex__version_cmp(void const *s1, void const *s2);

/* PS_STATE_PENDMASK requires some actions */
static void a_lex_update_pstate(void);

/* Evaluate a single command.
 * .le_add_history and .le_new_content will be updated upon success.
 * Command functions return 0 for success, 1 for error, and -1 for abort.
 * 1 or -1 aborts a load or source, a -1 aborts the interactive command loop */
static int a_lex_evaluate(struct a_lex_eval_ctx *evp);

/* Get first-fit, or NULL */
static struct a_lex_cmd const *a_lex__firstfit(char const *comm);

/* Branch here on hangup signal and simulate "exit" */
static void a_lex_hangup(int s);

/* The following gets called on receipt of an interrupt.  Close all open files
 * except 0, 1, 2, and the temporary.  Also, unstack all source files */
static void a_lex_onintr(int s);

/* Pop the current input back to the previous level.  Update the program state.
 * If the argument is TRUM1 then we don't alert and error out if the stack
 * doesn't exist at all */
static void a_lex_unstack(bool_t eval_error);

/* `source' and `source_if' (if silent_error: no pipes allowed, then) */
static bool_t a_lex_source_file(char const *file, bool_t silent_error);

/* System resource file load()ing or -X command line option array traversal */
static bool_t a_lex_load(struct a_lex_input_stack *lip);

/* A simplified command loop for recursed state machines */
static bool_t a_commands_recursive(enum n_lexinput_flags lif);

/* List of all commands, and list of commands which are specially treated
 * and deduced in _evaluate(), but we need a list for _c_list() and
 * print_comm_docstr() */
#ifdef HAVE_DOCSTRINGS
# define DS(S) , S
#else
# define DS(S)
#endif
static struct a_lex_cmd const a_lex_cmd_tab[] = {
#include "cmd_tab.h"
},
      a_lex_special_cmd_tab[] = {
   { "#", NULL, 0, 0, 0
      DS(N_("Comment command: ignore remaining (continuable) line")) },
   { "-", NULL, 0, 0, 0
      DS(N_("Print out the preceding message")) }
};
#undef DS

static char *
a_lex_isolate(char const *comm){
   NYD2_ENTER;
   while(*comm != '\0' &&
         strchr("~|? \t0123456789&%@$^.:/-+*'\",;(`", *comm) == NULL)
      ++comm;
   NYD2_LEAVE;
   return n_UNCONST(comm);
}

static int
a_lex_c_ghost(void *v){
   struct a_lex_ghost *lgp, *gp;
   size_t i, cl, nl;
   char *cp;
   char const **argv;
   NYD_ENTER;

   argv = v;

   /* Show the list? */
   if(*argv == NULL){
      FILE *fp;

      if((fp = Ftmp(NULL, "ghost", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
         fp = stdout;

      for(i = 0, gp = a_lex_ghosts; gp != NULL; gp = gp->lg_next)
         fprintf(fp, "wysh ghost %s %s\n",
            gp->lg_name, n_shexp_quote_cp(gp->lg_cmd.s, TRU1));

      if(fp != stdout){
         page_or_print(fp, i);
         Fclose(fp);
      }
      goto jleave;
   }

   /* Verify the ghost name is a valid one */
   if(*argv[0] == '\0' || *a_lex_isolate(argv[0]) != '\0'){
      n_err(_("`ghost': can't canonicalize %s\n"),
         n_shexp_quote_cp(argv[0], FAL0));
      v = NULL;
      goto jleave;
   }

   /* Show command of single ghost? */
   if(argv[1] == NULL){
      for(gp = a_lex_ghosts; gp != NULL; gp = gp->lg_next)
         if(!strcmp(argv[0], gp->lg_name)){
            printf("wysh ghost %s %s\n",
               gp->lg_name, n_shexp_quote_cp(gp->lg_cmd.s, TRU1));
            goto jleave;
         }
      n_err(_("`ghost': no such alias: %s\n"), argv[0]);
      v = NULL;
      goto jleave;
   }

   /* Define command for ghost: verify command content */
   for(cl = 0, i = 1; (cp = n_UNCONST(argv[i])) != NULL; ++i)
      if(*cp != '\0')
         cl += strlen(cp) +1; /* SP or NUL */
   if(cl == 0){
      n_err(_("`ghost': empty command arguments after %s\n"), argv[0]);
      v = NULL;
      goto jleave;
   }

   /* If the ghost already exists, recreate */
   for(lgp = NULL, gp = a_lex_ghosts; gp != NULL; lgp = gp, gp = gp->lg_next)
      if(!strcmp(gp->lg_name, argv[0])){
         if(lgp != NULL)
            lgp->lg_next = gp->lg_next;
         else
            a_lex_ghosts = gp->lg_next;
         free(gp);
         break;
      }

   nl = strlen(argv[0]) +1;
   gp = smalloc(sizeof(*gp) - n_VFIELD_SIZEOF(struct a_lex_ghost, lg_name) +
         nl + cl);
   gp->lg_next = a_lex_ghosts;
   a_lex_ghosts = gp;
   memcpy(gp->lg_name, argv[0], nl);
   cp = gp->lg_cmd.s = gp->lg_name + nl;
   gp->lg_cmd.l = --cl;

   while(*++argv != NULL)
      if((i = strlen(*argv)) > 0){
         memcpy(cp, *argv, i);
         cp += i;
         *cp++ = ' ';
      }
   *--cp = '\0';
jleave:
   NYD_LEAVE;
   return v == NULL;
}

static int
a_lex_c_unghost(void *v){
   struct a_lex_ghost *lgp, *gp;
   char const **argv, *cp;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = v;

   while((cp = *argv++) != NULL){
      if(cp[0] == '*' && cp[1] == '\0'){
         while((gp = a_lex_ghosts) != NULL){
            a_lex_ghosts = gp->lg_next;
            free(gp);
         }
      }else{
         for(lgp = NULL, gp = a_lex_ghosts; gp != NULL;
               lgp = gp, gp = gp->lg_next)
            if(!strcmp(gp->lg_name, cp)){
               if(lgp != NULL)
                  lgp->lg_next = gp->lg_next;
               else
                  a_lex_ghosts = gp->lg_next;
               free(gp);
               goto jouter;
            }
         n_err(_("`unghost': no such alias: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         rv = 1;
jouter:  ;
      }
   }
   NYD_LEAVE;
   return rv;
}

static int
a_lex_c_list(void *v){
   FILE *fp;
   struct a_lex_cmd const **cpa, *cp, **cursor;
   size_t l, i;
   NYD_ENTER;

   i = n_NELEM(a_lex_cmd_tab) + n_NELEM(a_lex_special_cmd_tab) +1;
   cpa = salloc(sizeof(cp) * i);

   for(i = 0; i < n_NELEM(a_lex_cmd_tab); ++i)
      cpa[i] = &a_lex_cmd_tab[i];
   /* C99 */{
      size_t j;

      for(j = 0; j < n_NELEM(a_lex_special_cmd_tab); ++i, ++j)
         cpa[i] = &a_lex_special_cmd_tab[j];
   }
   cpa[i] = NULL;

   /* C99 */{
      char const *xcp = v;

      if(*xcp == '\0')
         qsort(cpa, i, sizeof(xcp), &a_lex__pcmd_cmp);
   }

   if((fp = Ftmp(NULL, "list", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
      fp = stdout;

   fprintf(fp, _("Commands are:\n"));
   l = 1;
   for(i = 0, cursor = cpa; (cp = *cursor++) != NULL;){
      if(cp->lc_func == &c_cmdnotsupp)
         continue;
      if(options & OPT_D_V){
         char const *argt;

         switch(cp->lc_argtype & ARG_ARGMASK){
         case ARG_MSGLIST: argt = N_("message-list"); break;
         case ARG_STRLIST: argt = N_("a \"string\""); break;
         case ARG_RAWLIST: argt = N_("old-style quoting"); break;
         case ARG_NOLIST: argt = N_("no arguments"); break;
         case ARG_NDMLIST: argt = N_("message-list (without a default)"); break;
         case ARG_WYSHLIST: argt = N_("sh(1)ell-style quoting"); break;
         default: argt = N_("`wysh' for sh(1)ell-style quoting"); break;
         }
#ifdef HAVE_DOCSTRINGS
         fprintf(fp, _("`%s'.  Argument type: %s.\n\t%s\n"),
            cp->lc_name, V_(argt), V_(cp->lc_doc));
         l += 2;
#else
         fprintf(fp, "`%s' (%s)\n", cp->lc_name, argt);
         ++l;
#endif
      }else{
         size_t j = strlen(cp->lc_name) + 2;

         if((i += j) > 72){
            i = j;
            fprintf(fp, "\n");
            ++l;
         }
         fprintf(fp, (*cursor != NULL ? "%s, " : "%s\n"), cp->lc_name);
      }
   }

   if(fp != stdout){
      page_or_print(fp, l);
      Fclose(fp);
   }
   NYD_LEAVE;
   return 0;
}

static int
a_lex__pcmd_cmp(void const *s1, void const *s2){
   struct a_lex_cmd const * const *cp1, * const *cp2;
   int rv;
   NYD2_ENTER;

   cp1 = s1;
   cp2 = s2;
   rv = strcmp((*cp1)->lc_name, (*cp2)->lc_name);
   NYD2_LEAVE;
   return rv;
}

static int
a_lex_c_help(void *v){
   int rv;
   char *arg;
   NYD_ENTER;

   /* Help for a single command? */
   if((arg = *(char**)v) != NULL){
      struct a_lex_ghost const *gp;
      struct a_lex_cmd const *cp, *cpmax;

      /* Ghosts take precedence */
      for(gp = a_lex_ghosts; gp != NULL; gp = gp->lg_next)
         if(!strcmp(arg, gp->lg_name)){
            printf("%s -> ", arg);
            arg = gp->lg_cmd.s;
            break;
         }

      cpmax = &(cp = a_lex_cmd_tab)[n_NELEM(a_lex_cmd_tab)];
jredo:
      for(; PTRCMP(cp, <, cpmax); ++cp){
#ifdef HAVE_DOCSTRINGS
# define a_DS V_(cp->lc_doc)
#else
# define a_DS ""
#endif
         if(!strcmp(arg, cp->lc_name))
            printf("%s: %s", arg, a_DS);
         else if(is_prefix(arg, cp->lc_name))
            printf("%s (%s): %s", arg, cp->lc_name, a_DS);
         else
            continue;

         if(options & OPT_D_V){
            char const *atp;

            switch(cp->lc_argtype & ARG_ARGMASK){
            case ARG_MSGLIST: atp = N_("message-list"); break;
            case ARG_STRLIST: atp = N_("a \"string\""); break;
            case ARG_RAWLIST: atp = N_("old-style quoting"); break;
            case ARG_NOLIST: atp = N_("no arguments"); break;
            case ARG_NDMLIST: atp = N_("message-list (no default)"); break;
            case ARG_WYSHLIST: atp = N_("sh(1)ell-style quoting"); break;
            default: atp = N_("`wysh' for sh(1)ell-style quoting"); break;
            }
#ifdef HAVE_DOCSTRINGS
            printf(_("\n\tArgument type: %s"), V_(atp));
#else
            printf(_("argument type: %s"), V_(atp));
#endif
#undef a_DS
         }
         putchar('\n');
         rv = 0;
         goto jleave;
      }

      if(PTRCMP(cpmax, ==, &a_lex_cmd_tab[n_NELEM(a_lex_cmd_tab)])){
         cpmax = &(cp = a_lex_special_cmd_tab)[n_NELEM(a_lex_special_cmd_tab)];
         goto jredo;
      }

      if(gp != NULL){
         printf("%s\n", n_shexp_quote_cp(arg, TRU1));
         rv = 0;
      }else{
         n_err(_("Unknown command: `%s'\n"), arg);
         rv = 1;
      }
   }else{
      /* Very ugly, but take care for compiler supported string lengths :( */
      fputs(progname, stdout);
      fputs(_(
         " commands -- <msglist> denotes message specifications,\n"
         "e.g., 1-5, :n or ., separated by spaces:\n"), stdout);
      fputs(_(
"\n"
"type <msglist>         type (alias: `print') messages (honour `retain' etc.)\n"
"Type <msglist>         like `type' but always show all headers\n"
"next                   goto and type next message\n"
"from <msglist>         (search and) print header summary for the given list\n"
"headers                header summary for messages surrounding \"dot\"\n"
"delete <msglist>       delete messages (can be `undelete'd)\n"),
         stdout);

      fputs(_(
"\n"
"save <msglist> folder  append messages to folder and mark as saved\n"
"copy <msglist> folder  like `save', but don't mark them (`move' moves)\n"
"write <msglist> file   write message contents to file (prompts for parts)\n"
"Reply <msglist>        reply to message senders only\n"
"reply <msglist>        like `Reply', but address all recipients\n"
"Lreply <msglist>       forced mailing-list `reply' (see `mlist')\n"),
         stdout);

      fputs(_(
"\n"
"mail <recipients>      compose a mail for the given recipients\n"
"file folder            change to another mailbox\n"
"File folder            like `file', but open readonly\n"
"quit                   quit and apply changes to the current mailbox\n"
"xit or exit            like `quit', but discard changes\n"
"!shell command         shell escape\n"
"list [<anything>]      all available commands [in search order]\n"),
         stdout);

      rv = (ferror(stdout) != 0);
   }
jleave:
   NYD_LEAVE;
   return rv;
}

static int
a_lex_c_quit(void *v){
   NYD_ENTER;
   n_UNUSED(v);

   /* If we are PS_SOURCING, then return 1 so _evaluate() can handle it.
    * Otherwise return -1 to abort command loop */
   pstate |= PS_EXIT;
   NYD_LEAVE;
   return 0;
}

static int
a_lex_c_version(void *v){
   int longest, rv;
   char *iop;
   char const *cp, **arr;
   size_t i, i2;
   NYD_ENTER;
   n_UNUSED(v);

   printf(_("%s version %s\nFeatures included (+) or not (-)\n"),
      uagent, ok_vlook(version));

   /* *features* starts with dummy byte to avoid + -> *folder* expansions */
   i = strlen(cp = &ok_vlook(features)[1]) +1;
   iop = salloc(i);
   memcpy(iop, cp, i);

   arr = salloc(sizeof(cp) * VAL_FEATURES_CNT);
   for(longest = 0, i = 0; (cp = n_strsep(&iop, ',', TRU1)) != NULL; ++i){
      arr[i] = cp;
      i2 = strlen(cp);
      longest = n_MAX(longest, (int)i2);
   }
   qsort(arr, i, sizeof(cp), &a_lex__version_cmp);

   for(++longest, i2 = 0; i-- > 0;){
      cp = *(arr++);
      printf("%-*s ", longest, cp);
      i2 += longest;
      if(UICMP(z, ++i2 + longest, >=, scrnwidth) || i == 0){
         i2 = 0;
         putchar('\n');
      }
   }

   if((rv = ferror(stdout) != 0))
      clearerr(stdout);
   NYD_LEAVE;
   return rv;
}

static int
a_lex__version_cmp(void const *s1, void const *s2){
   char const * const *cp1, * const *cp2;
   int rv;
   NYD2_ENTER;

   cp1 = s1;
   cp2 = s2;
   rv = strcmp(&(*cp1)[1], &(*cp2)[1]);
   NYD2_LEAVE;
   return rv;
}

static void
a_lex_update_pstate(void){
   NYD_ENTER;

   if(pstate & PS_SIGWINCH_PEND){
      char buf[32];

      snprintf(buf, sizeof buf, "%d", scrnwidth);
      ok_vset(COLUMNS, buf);
      snprintf(buf, sizeof buf, "%d", scrnheight);
      ok_vset(LINES, buf);
   }

   pstate &= ~PS_PSTATE_PENDMASK;
   NYD_LEAVE;
}

static int
a_lex_evaluate(struct a_lex_eval_ctx *evp){
   /* xxx old style(9), but also old code */
   struct str line;
   char _wordbuf[2], *arglist[MAXARGC], *cp, *word;
   struct a_lex_ghost *gp;
   struct a_lex_cmd const *cmd;
   int c, e;
   bool_t wysh;
   NYD_ENTER;

   wysh = FAL0;
   e = 1;
   cmd = NULL;
   gp = NULL;
   line = evp->le_line; /* XXX don't change original (buffer pointer) */
   assert(line.s[line.l] == '\0');
   evp->le_add_history = FAL0;
   evp->le_new_content = NULL;

   /* Command ghosts that refer to shell commands or macro expansion restart */
jrestart:

   /* Strip the white space away from end and beginning of command */
   if(line.l > 0){
      size_t i = line.l;

      for(cp = &line.s[i -1]; spacechar(*cp); --cp)
         --i;
      line.l = i;
   }
   for(cp = line.s; spacechar(*cp); ++cp)
      ;
   line.l -= PTR2SIZE(cp - line.s);

   /* Ignore null commands (comments) */
   if(*cp == '#')
      goto jleave0;

   /* Handle ! differently to get the correct lexical conventions */
   arglist[0] = cp;
   if(*cp == '!')
      ++cp;
   /* Isolate the actual command; since it may not necessarily be
    * separated from the arguments (as in `p1') we need to duplicate it to
    * be able to create a NUL terminated version.
    * We must be aware of several special one letter commands here */
   else if((cp = a_lex_isolate(cp)) == arglist[0] &&
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
   if(*word == '\0'){
      if((pstate & PS_ROBOT) || gp != NULL)
         goto jleave0;
      cmd = a_lex_cmd_tab + 0;
      goto jexec;
   }

   /* XXX It may be the argument parse adjuster */
   if(!wysh && c == sizeof("wysh") -1 && !asccasecmp(word, "wysh")){
      wysh = TRU1;
      line.s = cp;
      goto jrestart;
   }

   /* If this is the first evaluation, check command ghosts */
   if(gp == NULL){
      /* TODO relink list head, so it's sorted on usage over time?
       * TODO in fact, there should be one hashmap over all commands and ghosts
       * TODO so that the lookup could be made much more efficient than it is
       * TODO now (two adjacent list searches! */
      for(gp = a_lex_ghosts; gp != NULL; gp = gp->lg_next)
         if(!strcmp(word, gp->lg_name)){
            if(line.l > 0){
               size_t i;

               i = gp->lg_cmd.l;
               line.s = salloc(i + line.l +1);
               memcpy(line.s, gp->lg_cmd.s, i);
               memcpy(line.s + i, cp, line.l);
               line.s[i += line.l] = '\0';
               line.l = i;
            }else{
               line.s = gp->lg_cmd.s;
               line.l = gp->lg_cmd.l;
            }
            goto jrestart;
         }
   }

   if((cmd = a_lex__firstfit(word)) == NULL || cmd->lc_func == &c_cmdnotsupp){
      bool_t s;

      if(!(s = condstack_isskip()) || (options & OPT_D_V))
         n_err(_("Unknown command%s: `%s'\n"),
            (s ? _(" (ignored due to `if' condition)") : ""), word);
      if(s)
         goto jleave0;
      if(cmd != NULL){
         c_cmdnotsupp(NULL);
         cmd = NULL;
      }
      goto jleave;
   }

   /* See if we should execute the command -- if a conditional we always
    * execute it, otherwise, check the state of cond */
jexec:
   if(!(cmd->lc_argtype & ARG_F) && condstack_isskip())
      goto jleave0;

   /* Process the arguments to the command, depending on the type it expects */
   if(!(cmd->lc_argtype & ARG_M) && (options & OPT_SENDMODE)){
      n_err(_("May not execute `%s' while sending\n"), cmd->lc_name);
      goto jleave;
   }
   if((cmd->lc_argtype & ARG_S) && !(pstate & PS_STARTED)){
      n_err(_("May not execute `%s' during startup\n"), cmd->lc_name);
      goto jleave;
   }
   if((cmd->lc_argtype & ARG_I) &&
         !(options & (OPT_INTERACTIVE | OPT_BATCH_FLAG))){
      n_err(_("May not execute `%s' unless interactive or in batch mode\n"),
         cmd->lc_name);
      goto jleave;
   }
   if((cmd->lc_argtype & ARG_R) && (pstate & PS_RECURSED)){
      n_err(_("Cannot invoke `%s' when in recursed mode (e.g., composing)\n"),
         cmd->lc_name);
      goto jleave;
   }

   if((cmd->lc_argtype & ARG_W) && !(mb.mb_perm & MB_DELE)){
      n_err(_("May not execute `%s' -- message file is read only\n"),
         cmd->lc_name);
      goto jleave;
   }
   if((cmd->lc_argtype & ARG_A) && mb.mb_type == MB_VOID){
      n_err(_("Cannot execute `%s' without active mailbox\n"), cmd->lc_name);
      goto jleave;
   }

   if(cmd->lc_argtype & ARG_O)
      OBSOLETE2(_("this command will be removed"), cmd->lc_name);
   if(cmd->lc_argtype & ARG_V)
      temporary_arg_v_store = NULL;

   if(wysh && (cmd->lc_argtype & ARG_ARGMASK) != ARG_WYRALIST)
      n_err(_("`wysh' prefix doesn't affect `%s'\n"), cmd->lc_name);
   /* TODO v15: strip PS_ARGLIST_MASK off, just in case the actual command
    * TODO doesn't use any of those list commands which strip this mask,
    * TODO and for now we misuse bits for checking relation to history;
    * TODO argument state should be property of a per-command carrier instead */
   pstate &= ~PS_ARGLIST_MASK;
   switch(cmd->lc_argtype & ARG_ARGMASK){
   case ARG_MSGLIST:
      /* Message list defaulting to nearest forward legal message */
      if(n_msgvec == NULL)
         goto je96;
      if((c = getmsglist(cp, n_msgvec, cmd->lc_msgflag)) < 0)
         break;
      if(c == 0){
         if((n_msgvec[0] = first(cmd->lc_msgflag, cmd->lc_msgmask)) != 0)
            n_msgvec[1] = 0;
      }
      if(n_msgvec[0] == 0){
         if(!(pstate & PS_HOOK_MASK))
            printf(_("No applicable messages\n"));
         break;
      }
      e = (*cmd->lc_func)(n_msgvec);
      break;

   case ARG_NDMLIST:
      /* Message list with no defaults, but no error if none exist */
      if(n_msgvec == NULL){
je96:
         n_err(_("Invalid use of message list\n"));
         break;
      }
      if((c = getmsglist(cp, n_msgvec, cmd->lc_msgflag)) < 0)
         break;
      e = (*cmd->lc_func)(n_msgvec);
      break;

   case ARG_STRLIST:
      /* Just the straight string, with leading blanks removed */
      while(whitechar(*cp))
         ++cp;
      e = (*cmd->lc_func)(cp);
      break;

   case ARG_WYSHLIST:
      c = 1;
      if(0){
         /* FALLTHRU */
   case ARG_WYRALIST:
         c = wysh ? 1 : 0;
         if(0){
   case ARG_RAWLIST:
            c = 0;
         }
      }

      if((c = getrawlist((c != 0), arglist, n_NELEM(arglist), cp, line.l)) < 0){
         n_err(_("Invalid argument list\n"));
         break;
      }
      if(c < cmd->lc_minargs){
         n_err(_("`%s' requires at least %d arg(s)\n"),
            cmd->lc_name, cmd->lc_minargs);
         break;
      }
#undef lc_minargs
      if(c > cmd->lc_maxargs){
         n_err(_("`%s' takes no more than %d arg(s)\n"),
            cmd->lc_name, cmd->lc_maxargs);
         break;
      }
#undef lc_maxargs
      e = (*cmd->lc_func)(arglist);
      break;

   case ARG_NOLIST:
      /* Just the constant zero, for exiting, eg. */
      e = (*cmd->lc_func)(0);
      break;

   default:
      DBG( n_panic(_("Implementation error: unknown argument type: %d"),
         cmd->lc_argtype & ARG_ARGMASK); )
      goto jleave0;
   }

   if(e == 0 && (cmd->lc_argtype & ARG_V) &&
         (cp = temporary_arg_v_store) != NULL){
      temporary_arg_v_store = NULL;
      evp->le_new_content = cp;
      goto jleave0;
   }
   if(!(cmd->lc_argtype & ARG_H))
      evp->le_add_history = (((cmd->lc_argtype & ARG_G) ||
            (pstate & PS_MSGLIST_GABBY)) ? TRUM1 : TRU1);

jleave:
   /* C99 */{
      bool_t reset = !(pstate & PS_ROOT);

      pstate |= PS_ROOT;
      ok_vset(_exit_status, (e == 0 ? "0" : "1")); /* TODO num=1 +real value! */
      if(reset)
         pstate &= ~PS_ROOT;
   }

   /* Exit the current source file on error TODO what a mess! */
   if(e == 0)
      pstate &= ~PS_EVAL_ERROR;
   else{
      pstate |= PS_EVAL_ERROR;
      if(e < 0 || (pstate & PS_ROBOT)){ /* FIXME */
         e = 1;
         goto jret;
      }
      goto jret0;
   }

   if(cmd == NULL)
      goto jret0;
   if((cmd->lc_argtype & ARG_P) && ok_blook(autoprint))
      if(visible(dot)){
         line.s = savestr("type");
         line.l = sizeof("type") -1;
         gp = (struct a_lex_ghost*)-1; /* Avoid `ghost' interpretation */
         goto jrestart;
      }

   if(!(pstate & (PS_SOURCING | PS_HOOK_MASK)) && !(cmd->lc_argtype & ARG_T))
      pstate |= PS_SAW_COMMAND;
jleave0:
   pstate &= ~PS_EVAL_ERROR;
jret0:
   e = 0;
jret:
/*
fprintf(stderr, "a_lex_evaluate returns %d for <%s>\n",e,line.s);
*/
   NYD_LEAVE;
   return e;
}

static struct a_lex_cmd const *
a_lex__firstfit(char const *comm){ /* TODO *hashtable*! linear list search!!! */
   struct a_lex_cmd const *cp;
   NYD2_ENTER;

   for(cp = a_lex_cmd_tab;
         PTRCMP(cp, <, &a_lex_cmd_tab[n_NELEM(a_lex_cmd_tab)]); ++cp)
      if(*comm == *cp->lc_name && is_prefix(comm, cp->lc_name))
         goto jleave;
   cp = NULL;
jleave:
   NYD2_LEAVE;
   return cp;
}

static void
a_lex_hangup(int s){
   NYD_X; /* Signal handler */
   n_UNUSED(s);
   /* nothing to do? */
   exit(EXIT_ERR);
}

static void
a_lex_onintr(int s){ /* TODO block signals while acting */
   NYD_X; /* Signal handler */
   n_UNUSED(s);

   safe_signal(SIGINT, a_lex_onintr);

   termios_state_reset();
   close_all_files(); /* FIXME .. of current level ONLU! */
   if(image >= 0){
      close(image);
      image = -1;
   }

   a_lex_unstack(TRUM1);

   if(interrupts != 1)
      n_err_sighdl(_("Interrupt\n"));
   safe_signal(SIGPIPE, a_lex_oldpipe);
   siglongjmp(srbuf, 0); /* FIXME get rid */
}

static void
a_lex_unstack(bool_t eval_error){
   struct a_lex_input_stack *lip;
   NYD_ENTER;

   if((lip = a_lex_input) == NULL){
      n_memory_reset();

      /* If called from a_lex_onintr(), be silent FIXME */
      pstate &= ~(PS_SOURCING | PS_ROBOT);
      if(eval_error == TRUM1 || !(pstate & PS_STARTED))
         goto jleave;
      goto jerr;
   }

   if(lip->li_flags & a_LEX_MACRO){
      if(lip->li_flags & a_LEX_MACRO_FREE_DATA){
         char **lp;

         while(*(lp = &lip->li_lines[lip->li_loff]) != NULL){
            free(*lp);
            ++lip->li_loff;
         }
         /* Part of lip's memory chunk, then */
         if(!(lip->li_flags & a_LEX_MACRO_CMD))
            free(lip->li_lines);
      }
   }else{
      if(lip->li_flags & a_LEX_PIPE)
         /* XXX command manager should -TERM then -KILL instead of hoping
          * XXX for exit of provider due to EPIPE / SIGPIPE */
         Pclose(lip->li_file, TRU1);
      else
         Fclose(lip->li_file);
   }

   if(!condstack_take(lip->li_cond)){
      n_err(_("Unmatched `if' at end of %s %s\n"),
         ((lip->li_flags & a_LEX_MACRO
          ? (lip->li_flags & a_LEX_MACRO_CMD ? _("command") : _("macro"))
          : _("`source'd file"))),
         lip->li_name);
      eval_error = TRU1;
   }

   n_memory_autorec_pop(&lip->li_autorecmem[0]);

   if((a_lex_input = lip->li_outer) == NULL){
      pstate &= ~(PS_SOURCING | PS_ROBOT);
   }else{
      if((a_lex_input->li_flags & (a_LEX_MACRO | a_LEX_SUPER_MACRO)) ==
            (a_LEX_MACRO | a_LEX_SUPER_MACRO))
         pstate &= ~PS_SOURCING;
      assert(pstate & PS_ROBOT);
   }

   if(eval_error)
      goto jerr;
jleave:
   if(lip != NULL && (lip->li_flags & a_LEX_FREE))
      free(lip);
   if(n_UNLIKELY(a_lex_input != NULL && eval_error == TRUM1))
      a_lex_unstack(TRUM1);
   NYD_LEAVE;
   return;

jerr:
   if(lip != NULL){
      if(options & OPT_D_V)
         n_alert(_("Stopped %s %s due to errors%s"),
            (pstate & PS_STARTED
             ? (lip->li_flags & a_LEX_MACRO
                ? (lip->li_flags & a_LEX_MACRO_CMD
                   ? _("evaluating command") : _("evaluating macro"))
                : (lip->li_flags & a_LEX_PIPE
                   ? _("executing `source'd pipe")
                   : _("loading `source'd file")))
             : (lip->li_flags & a_LEX_MACRO
                ? (lip->li_flags & a_LEX_MACRO_X_OPTION
                   ? _("evaluating command line") : _("evaluating macro"))
                : _("loading initialization resource"))),
            lip->li_name,
            (options & OPT_DEBUG ? "" : _(" (enable *debug* for trace)")));
   }

   if(!(options & OPT_INTERACTIVE) && !(pstate & PS_STARTED)){
      if(options & OPT_D_V)
         n_alert(_("Non-interactive, bailing out due to errors "
            "in startup load phase"));
      exit(EXIT_ERR);
   }
   goto jleave;
}

static bool_t
a_lex_source_file(char const *file, bool_t silent_error){
   struct a_lex_input_stack *lip;
   size_t nlen;
   char *nbuf;
   bool_t ispipe;
   FILE *fip;
   NYD_ENTER;

   fip = NULL;

   /* Being a command argument file is space-trimmed *//* TODO v15 with
    * TODO WYRALIST this is no longer necessary true, and for that we
    * TODO don't set _PARSE_TRIMSPACE because we cannot! -> cmd_tab.h!! */
#if 0
   ((ispipe = (!silent_error && (nlen = strlen(file)) > 0 &&
         file[--nlen] == '|')))
#else
   ispipe = FAL0;
   if(!silent_error)
      for(nlen = strlen(file); nlen > 0;){
         char c;

         c = file[--nlen];
         if(!blankchar(c)){
            if(c == '|'){
               nbuf = savestrbuf(file, nlen);
               ispipe = TRU1;
               break;
            }
         }
      }
#endif

   if(ispipe){
      if((fip = Popen(nbuf /* #if 0 above = savestrbuf(file, nlen)*/, "r",
            ok_vlook(SHELL), NULL, COMMAND_FD_NULL)) == NULL){
         if(!silent_error || (options & OPT_D_V))
            n_perr(nbuf, 0);
         goto jleave;
      }
   }else if((nbuf = fexpand(file, FEXP_LOCAL)) == NULL)
      goto jleave;
   else if((fip = Fopen(nbuf, "r")) == NULL){
      if(!silent_error || (options & OPT_D_V))
         n_perr(nbuf, 0);
      goto jleave;
   }

   lip = smalloc(sizeof(*lip) -
         n_VFIELD_SIZEOF(struct a_lex_input_stack, li_name) +
         (nlen = strlen(nbuf) +1));
   lip->li_outer = a_lex_input;
   lip->li_file = fip;
   lip->li_cond = condstack_release();
   n_memory_autorec_push(&lip->li_autorecmem[0]);
   lip->li_flags = (ispipe ? a_LEX_FREE | a_LEX_PIPE : a_LEX_FREE) |
         (a_lex_input != NULL && (a_lex_input->li_flags & a_LEX_SUPER_MACRO)
          ? a_LEX_SUPER_MACRO : 0);
   memcpy(lip->li_name, nbuf, nlen);

   pstate |= PS_SOURCING | PS_ROBOT;
   a_lex_input = lip;
   a_commands_recursive(n_LEXINPUT_NONE | n_LEXINPUT_NL_ESC);
/* FIXME return TRUM1 if file can't be opened, FAL0 on eval error */
jleave:
   NYD_LEAVE;
   return silent_error ? TRU1 : (fip != NULL);
}

static bool_t
a_lex_load(struct a_lex_input_stack *lip){
   bool_t rv;
   NYD2_ENTER;

   assert(!(pstate & PS_STARTED));
   assert(a_lex_input == NULL);

   /* POSIX:
    *    Any errors in the start-up file shall either cause mailx to terminate
    *    with a diagnostic message and a non-zero status or to continue after
    *    writing a diagnostic message, ignoring the remainder of the lines in
    *    the start-up file. */
   lip->li_cond = condstack_release();
   n_memory_autorec_push(&lip->li_autorecmem[0]);

/* FIXME won't work for now (PS_ROBOT needs PS_SOURCING anyway)
   pstate |= PS_ROBOT |
         (lip->li_flags & a_LEX_MACRO_X_OPTION ? 0 : PS_SOURCING);
*/
   pstate |= PS_ROBOT | PS_SOURCING;
   if(options & OPT_D_V)
      n_err(_("Loading %s\n"), n_shexp_quote_cp(lip->li_name, FAL0));
   a_lex_input = lip;
   if(!(rv = n_commands())){
      if(!(options & OPT_INTERACTIVE)){
         if(options & OPT_D_V)
            n_alert(_("Non-interactive program mode, forced exit"));
         exit(EXIT_ERR);
      }
   }
   /* PS_EXIT handled by callers */
   NYD2_LEAVE;
   return rv;
}

static bool_t
a_commands_recursive(enum n_lexinput_flags lif){
   struct a_lex_eval_ctx ev;
   bool_t rv;
   NYD2_ENTER;

   memset(&ev, 0, sizeof ev);

/* FIXME sigkondom */
   n_COLOUR( n_colour_env_push(); )
   rv = TRU1;
   for(;;){
      int n;

      /* Read a line of commands and handle end of file specially */
      ev.le_line.l = ev.le_line_size;
      n = n_lex_input(lif, NULL, &ev.le_line.s, &ev.le_line.l,
            ev.le_new_content);
      ev.le_line_size = (ui32_t)ev.le_line.l;
      ev.le_line.l = (ui32_t)n;

      if(n < 0)
         break;

      if(a_lex_evaluate(&ev)){
         rv = FAL0;
         break;
      }
      n_memory_reset();

      if((options & OPT_BATCH_FLAG) && ok_blook(batch_exit_on_error)){
         if(exit_status != EXIT_OK)
            break;
      }
   }
   a_lex_unstack(!rv);
   n_COLOUR( n_colour_env_pop(FAL0); )

   if(ev.le_line.s != NULL)
      free(ev.le_line.s);
   NYD2_LEAVE;
   return rv;
}

FL bool_t
n_commands(void){ /* FIXME */
   struct a_lex_eval_ctx ev;
   int n;
   bool_t volatile rv;
   NYD_ENTER;

   rv = TRU1;

   if (!(pstate & PS_SOURCING)) {
      if (safe_signal(SIGINT, SIG_IGN) != SIG_IGN)
         safe_signal(SIGINT, &a_lex_onintr);
      if (safe_signal(SIGHUP, SIG_IGN) != SIG_IGN)
         safe_signal(SIGHUP, &a_lex_hangup);
   }
   a_lex_oldpipe = safe_signal(SIGPIPE, SIG_IGN);
   safe_signal(SIGPIPE, a_lex_oldpipe);

   memset(&ev, 0, sizeof ev);

   (void)sigsetjmp(srbuf, 1); /* FIXME get rid */
   for (;;) {
      char *temporary_orig_line; /* XXX eval_ctx.le_line not yet constant */

      n_COLOUR( n_colour_env_pop(TRU1); )

      /* TODO Unless we have our signal manager (or however we do it) child
       * TODO processes may have time slots where their execution isn't
       * TODO protected by signal handlers (in between start and setup
       * TODO completed).  close_all_files() is only called from onintr()
       * TODO so those may linger possibly forever */
      if(!(pstate & PS_SOURCING))
         close_all_files();

      interrupts = 0;

      temporary_localopts_free(); /* XXX intermediate hack */

      n_memory_reset();

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
         if (ev.le_line.l > LINESIZE * 3) {
            free(ev.le_line.s); /* TODO pool! but what? */
            ev.le_line.s = NULL;
            ev.le_line.l = ev.le_line_size = 0;
         }
      }

      if (!(pstate & PS_SOURCING) && (options & OPT_INTERACTIVE)) {
         char *cp;

         cp = ok_vlook(newmail);
         if ((options & OPT_TTYIN) && cp != NULL) {
            struct stat st;

/* FIXME TEST WITH NOPOLL ETC. !!! */
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

         exit_status = EXIT_OK;
      }

      /* Read a line of commands and handle end of file specially */
jreadline:
      ev.le_line.l = ev.le_line_size;
      n = n_lex_input(n_LEXINPUT_CTX_DEFAULT | n_LEXINPUT_NL_ESC, NULL,
            &ev.le_line.s, &ev.le_line.l, ev.le_new_content);
      ev.le_line_size = (ui32_t)ev.le_line.l;
      ev.le_line.l = (ui32_t)n;

      if (n < 0) {
/* FIXME did unstack() when PS_SOURCING, only break with PS_LOADING*/
         if (!(pstate & PS_ROBOT) &&
               (options & OPT_INTERACTIVE) && ok_blook(ignoreeof)) {
            printf(_("*ignoreeof* set, use `quit' to quit.\n"));
            n_msleep(500, FAL0);
            continue;
         }
         break;
      }

      temporary_orig_line = ((pstate & PS_SOURCING) ||
         !(options & OPT_INTERACTIVE)) ? NULL
          : savestrbuf(ev.le_line.s, ev.le_line.l);
      pstate &= ~PS_HOOK_MASK;
      if (a_lex_evaluate(&ev)) {
         if (!(pstate & PS_STARTED)) /* TODO mess; join PS_EVAL_ERROR.. */
            rv = FAL0;
         break;
      }

      if ((options & OPT_BATCH_FLAG) && ok_blook(batch_exit_on_error)) {
         if (exit_status != EXIT_OK)
            break;
         if ((pstate & (PS_SOURCING | PS_EVAL_ERROR)) == PS_EVAL_ERROR) {
            exit_status = EXIT_ERR;
            break;
         }
      }
      if (!(pstate & PS_SOURCING) && (options & OPT_INTERACTIVE)) {
         if (ev.le_new_content != NULL)
            goto jreadline;
         /* *Can* happen since _evaluate() n_unstack()s on error! XXX no more */
         if (temporary_orig_line != NULL)
            n_tty_addhist(temporary_orig_line, (ev.le_add_history != TRU1));
      }

      if(pstate & PS_EXIT)
         break;
   }

   a_lex_unstack(!rv);

   if (ev.le_line.s != NULL)
      free(ev.le_line.s);
   NYD_LEAVE;
   return rv;
}

FL int
(n_lex_input)(enum n_lexinput_flags lif, char const *prompt, char **linebuf,
      size_t *linesize, char const *string n_MEMORY_DEBUG_ARGS){
   /* TODO readline: linebuf pool!; n_lex_input should return si64_t */
   struct n_string xprompt;
   FILE *ifile;
   bool_t doprompt, dotty;
   char const *iftype;
   int n, nold;
   NYD2_ENTER;

   /* Special case macro mode: never need to prompt, lines have always been
    * unfolded already */
   if(a_lex_input != NULL && (a_lex_input->li_flags & a_LEX_MACRO)){
      if(*linebuf != NULL)
         free(*linebuf);

      if((*linebuf = a_lex_input->li_lines[a_lex_input->li_loff]) == NULL){
         *linesize = 0;
         n = -1;
         goto jleave;
      }

      ++a_lex_input->li_loff;
      *linesize = strlen(*linebuf);
      if(!(a_lex_input->li_flags & a_LEX_MACRO_FREE_DATA))
         *linebuf = sbufdup(*linebuf, *linesize);

      iftype = (a_lex_input->li_flags & a_LEX_MACRO_X_OPTION)
            ? "-X OPTION" : "MACRO";
      n = (int)*linesize;
      pstate |= PS_READLINE_NL;
      goto jhave_dat;
   }
   pstate &= ~PS_READLINE_NL;

   iftype = (!(pstate & PS_STARTED) ? "LOAD"
          : (pstate & PS_SOURCING) ? "SOURCE" : "READ");
   doprompt = ((pstate & (PS_STARTED | PS_ROBOT)) == PS_STARTED &&
         (options & OPT_INTERACTIVE));
   dotty = (doprompt && !ok_blook(line_editor_disable));
   if(!doprompt)
      lif |= n_LEXINPUT_PROMPT_NONE;
   else{
      if(!dotty)
         n_string_creat_auto(&xprompt);
      if(prompt == NULL)
         lif |= n_LEXINPUT_PROMPT_EVAL;
   }

   /* Ensure stdout is flushed first anyway */
   if(!dotty && (lif & n_LEXINPUT_PROMPT_NONE))
      fflush(stdout);

   ifile = (a_lex_input != NULL) ? a_lex_input->li_file : stdin;
   assert(ifile != NULL);

   for(nold = n = 0;;){
      if(dotty){
         assert(ifile == stdin);
         if(string != NULL && (n = (int)strlen(string)) > 0){
            if(*linesize > 0)
               *linesize += n +1;
            else
               *linesize = (size_t)n + LINESIZE +1;
            *linebuf = (n_realloc)(*linebuf, *linesize n_MEMORY_DEBUG_ARGSCALL);
           memcpy(*linebuf, string, (size_t)n +1);
         }
         string = NULL;
         /* TODO if nold>0, don't redisplay the entire line!
          * TODO needs complete redesign ... */
         n = (n_tty_readline)(lif, prompt, linebuf, linesize, n
               n_MEMORY_DEBUG_ARGSCALL);
      }else{
         if(!(lif & n_LEXINPUT_PROMPT_NONE)){
            n_tty_create_prompt(&xprompt, prompt, lif);
            if(xprompt.s_len > 0){
               fwrite(xprompt.s_dat, 1, xprompt.s_len, stdout);
               fflush(stdout);
            }
         }

         n = (readline_restart)(ifile, linebuf, linesize, n
               n_MEMORY_DEBUG_ARGSCALL);

         if(n > 0 && nold > 0){
            int i = 0;
            char const *cp = *linebuf + nold;

            while(blankspacechar(*cp) && nold + i < n)
               ++cp, ++i;
            if(i > 0){
               memmove(*linebuf + nold, cp, n - nold - i);
               n -= i;
               (*linebuf)[n] = '\0';
            }
         }
      }

      if(n <= 0)
         break;

      /* POSIX says:
       *    An unquoted <backslash> at the end of a command line shall
       *    be discarded and the next line shall continue the command */
      if(!(lif & n_LEXINPUT_NL_ESC) || (*linebuf)[n - 1] != '\\'){
         if(dotty)
            pstate |= PS_READLINE_NL;
         break;
      }
      /* Definitely outside of quotes, thus the quoting rules are so that an
       * uneven number of successive backslashs at EOL is a continuation */
      if(n > 1){
         size_t i, j;

         for(j = 1, i = (size_t)n - 1; i-- > 0; ++j)
            if((*linebuf)[i] != '\\')
               break;
         if(!(j & 1))
            break;
      }
      (*linebuf)[nold = --n] = '\0';
      lif |= n_LEXINPUT_NL_FOLLOW;
   }

   if(n < 0)
      goto jleave;
   (*linebuf)[*linesize = n] = '\0';

jhave_dat:
#if 0
   if(lif & n_LEXINPUT_DROP_TRAIL_SPC){
      char *cp, c;
      size_t i;

      for(cp = &(*linebuf)[i = (size_t)n];; --i){
         c = *--cp;
         if(!blankspacechar(c))
            break;
      }
      (*linebuf)[n = (int)i] = '\0';
   }

   if(lif & n_LEXINPUT_DROP_LEAD_SPC){
      char *cp, c;
      size_t j, i;

      for(cp = &(*linebuf)[0], j = (size_t)n, i = 0; i < j; ++i){
         c = *cp++;
         if(!blankspacechar(c))
            break;
      }
      if(i > 0){
         memcpy(&(*linebuf)[0], &(*linebuf)[i], j -= i);
         (*linebuf)[n = (int)j] = '\0';
      }
   }
#endif /* 0 (notyet - must take care for backslash escaped space) */

   if(options & OPT_D_VV)
      n_err(_("%s %d bytes <%s>\n"), iftype, n, *linebuf);
jleave:
   if (pstate & PS_PSTATE_PENDMASK)
      a_lex_update_pstate();
   NYD2_LEAVE;
   return n;
}

FL char *
n_lex_input_cp(enum n_lexinput_flags lif, char const *prompt,
      char const *string){
   /* FIXME n_lex_input_cp_addhist(): leaks on sigjmp without linepool */
   size_t linesize;
   char *linebuf, *rv;
   int n;
   NYD2_ENTER;

   linesize = 0;
   linebuf = NULL;
   rv = NULL;

   n = n_lex_input(lif, prompt, &linebuf, &linesize, string);
   if(n > 0 && *(rv = savestrbuf(linebuf, (size_t)n)) != '\0' &&
         (lif & n_LEXINPUT_HIST_ADD) && (options & OPT_INTERACTIVE))
      n_tty_addhist(rv, ((lif & n_LEXINPUT_HIST_GABBY) != 0));

   if(linebuf != NULL)
      free(linebuf);
   NYD2_LEAVE;
   return rv;
}

FL void
n_load(char const *name){
   struct a_lex_input_stack *lip;
   size_t i;
   FILE *fip;
   NYD_ENTER;

   if(name == NULL || *name == '\0' || (fip = Fopen(name, "r")) == NULL)
      goto jleave;

   i = strlen(name) +1;
   lip = scalloc(1, sizeof(*lip) -
         n_VFIELD_SIZEOF(struct a_lex_input_stack, li_name) + i);
   lip->li_file = fip;
   lip->li_flags = a_LEX_FREE;
   memcpy(lip->li_name, name, i);

   a_lex_load(lip);
   pstate &= ~PS_EXIT;
jleave:
   NYD_LEAVE;
}

FL void
n_load_Xargs(char const **lines, size_t cnt){
   static char const name[] = "-X";

   ui8_t buf[sizeof(struct a_lex_input_stack) + sizeof name];
   char const *srcp, *xsrcp;
   char *cp;
   size_t imax, i, len;
   struct a_lex_input_stack *lip;
   NYD_ENTER;

   memset(buf, 0, sizeof buf);
   lip = (void*)buf;
   lip->li_flags = a_LEX_MACRO | a_LEX_MACRO_FREE_DATA |
         a_LEX_MACRO_X_OPTION | a_LEX_SUPER_MACRO;
   memcpy(lip->li_name, name, sizeof name);

   /* The problem being that we want to support reverse solidus newline
    * escaping also within multiline -X, i.e., POSIX says:
    *    An unquoted <backslash> at the end of a command line shall
    *    be discarded and the next line shall continue the command
    * Therefore instead of "lip->li_lines = n_UNCONST(lines)", duplicate the
    * entire lines array and set _MACRO_FREE_DATA */
   imax = cnt + 1;
   lip->li_lines = smalloc(sizeof(*lip->li_lines) * imax);

   /* For each of the input lines.. */
   for(i = len = 0, cp = NULL; cnt > 0;){
      bool_t keep;
      size_t j;

      if((j = strlen(srcp = *lines)) == 0){
         ++lines, --cnt;
         continue;
      }

      /* Separate one line from a possible multiline input string */
      if((xsrcp = memchr(srcp, '\n', j)) != NULL){
         *lines = &xsrcp[1];
         j = PTR2SIZE(xsrcp - srcp);
      }else
         ++lines, --cnt;

      /* The (separated) string may itself indicate soft newline escaping */
      if((keep = (srcp[j - 1] == '\\'))){
         size_t xj, xk;

         /* Need an uneven number of reverse solidus */
         for(xk = 1, xj = j - 1; xj-- > 0; ++xk)
            if(srcp[xj] != '\\')
               break;
         if(xk & 1)
            --j;
         else
            keep = FAL0;
      }

      /* Strip any leading WS from follow lines, then */
      if(cp != NULL)
         while(j > 0 && blankspacechar(*srcp))
            ++srcp, --j;

      if(j > 0){
         if(i + 2 >= imax){ /* TODO need a vector (main.c, here, ++) */
            imax += 4;
            lip->li_lines = n_realloc(lip->li_lines, sizeof(*lip->li_lines) *
                  imax);
         }
         lip->li_lines[i] = cp = n_realloc(cp, len + j +1);
         memcpy(&cp[len], srcp, j);
         cp[len += j] = '\0';

         if(!keep)
            ++i;
      }
      if(!keep)
         cp = NULL, len = 0;
   }
   if(cp != NULL){
      assert(i + 1 < imax);
      lip->li_lines[i++] = cp;
   }
   lip->li_lines[i] = NULL;

   a_lex_load(lip);
   if(pstate & PS_EXIT)
      exit(EXIT_OK);
   NYD_LEAVE;
}

FL int
c_source(void *v){
   int rv;
   NYD_ENTER;

   rv = (a_lex_source_file(*(char**)v, FAL0) == TRU1) ? 0 : 1;
   NYD_LEAVE;
   return rv;
}

FL int
c_source_if(void *v){ /* XXX obsolete?, support file tests in `if' etc.! */
   int rv;
   NYD_ENTER;

   rv = (a_lex_source_file(*(char**)v, TRU1) != FAL0) ? 0 : 1;
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_source_macro(enum n_lexinput_flags lif, char const *name, char **lines){
   struct a_lex_input_stack *lip;
   size_t i;
   int rv;
   NYD_ENTER;

   lip = smalloc(sizeof(*lip) -
         n_VFIELD_SIZEOF(struct a_lex_input_stack, li_name) +
         (i = strlen(name) +1));
   lip->li_outer = a_lex_input;
   lip->li_file = NULL;
   lip->li_cond = condstack_release();
   n_memory_autorec_push(&lip->li_autorecmem[0]);
   lip->li_flags = a_LEX_FREE | a_LEX_MACRO | a_LEX_MACRO_FREE_DATA |
         (a_lex_input == NULL || (a_lex_input->li_flags & a_LEX_SUPER_MACRO)
          ? a_LEX_SUPER_MACRO : 0);
   lip->li_loff = 0;
   lip->li_lines = lines;
   memcpy(lip->li_name, name, i);

   pstate |= PS_ROBOT;
   a_lex_input = lip;
   rv = a_commands_recursive(lif);
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_source_command(enum n_lexinput_flags lif, char const *cmd){
   struct a_lex_input_stack *lip;
   size_t i, ial;
   bool_t rv;
   NYD_ENTER;

   i = strlen(cmd);
   cmd = sbufdup(cmd, i++);
   ial = n_ALIGN(i);

   lip = smalloc(sizeof(*lip) -
         n_VFIELD_SIZEOF(struct a_lex_input_stack, li_name) +
         ial + 2*sizeof(char*));
   lip->li_outer = a_lex_input;
   lip->li_file = NULL;
   lip->li_cond = condstack_release();
   n_memory_autorec_push(&lip->li_autorecmem[0]);
   lip->li_flags = a_LEX_FREE | a_LEX_MACRO | a_LEX_MACRO_FREE_DATA |
         a_LEX_MACRO_CMD |
         (a_lex_input == NULL || (a_lex_input->li_flags & a_LEX_SUPER_MACRO)
          ? a_LEX_SUPER_MACRO : 0);
   lip->li_loff = 0;
   lip->li_lines = (void*)(lip->li_name + ial);
   lip->li_lines[0] = n_UNCONST(cmd); /* dup'ed above */
   lip->li_lines[1] = NULL;
   memcpy(lip->li_name, cmd, i);

   pstate |= PS_ROBOT;
   a_lex_input = lip;
   rv = a_commands_recursive(lif);
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_source_may_yield_control(void){
   return ((options & OPT_INTERACTIVE) &&
      (pstate & PS_STARTED) &&
      (!(pstate & PS_ROBOT) || (pstate & PS_RECURSED)) && /* Ok for ~: */
      (a_lex_input == NULL || a_lex_input->li_outer == NULL));
}

/* s-it-mode */
