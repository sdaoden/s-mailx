/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Command input, lexing and evaluation, resource file loading and `source'ing.
 *@ TODO n_PS_ROBOT requires yet n_PS_SOURCING, which REALLY sucks.
 *@ TODO Commands and ghosts deserve a hashmap.  Or so.
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
   /* TODO For simplicity this is yet _MACRO plus specialization overlay
    * TODO (_X_OPTION, _CMD) -- these should be types on their own! */
   a_LEX_MACRO_X_OPTION = 1<<4,  /* Macro indeed command line -X option */
   a_LEX_MACRO_CMD = 1<<5,       /* Macro indeed single-line: ~:COMMAND */
   /* TODO a_LEX_SLICE: the right way to support *on-compose-done-shell* would
    * TODO be a command_loop object that emits an on_read_line event, and
    * TODO have a special handler for the compose mode; with that, then,
    * TODO commands_recursive() would not call lex_evaluate() but
    * TODO CTX->on_read_line, and lex_evaluate() would be the standard impl.,
    * TODO whereas the COMMAND ESCAPE switch in collect.c would be another one.
    * TODO With this generic accmacvar.c:temporary_compose_mode_hook_call()
    * TODO could be dropped, and n_source_macro() could become extended,
    * TODO and/or we would add a n_source_anything(), which would allow special
    * TODO input handlers, special I/O input and output, special `localopts'
    * TODO etc., to be glued to the new execution context.  And all I/O all
    * TODO over this software should not use stdin/stdout, but CTX->in/out.
    * TODO The pstate must be a property of the current execution context, too.
    * TODO This not today. :(  For now we invent a special SLICE execution
    * TODO context overlay that at least allows to temporarily modify the
    * TODO global pstate, and the global stdin and stdout pointers.  HACK!
    * TODO This slice thing is very special and has to go again.  HACK!!
    * TODO a_lex_input() will drop it once it sees EOF (HACK!), but care for
    * TODO jumps must be taken by slice creators.  HACK!!!  But works. ;} */
   a_LEX_SLICE = 1<<6,
   /* TODO If it is none of those, we are sourcing or loading a file */

   a_LEX_FORCE_EOF = 1<<8,       /* lex_input() shall return EOF next */

   a_LEX_SUPER_MACRO = 1<<16     /* *Not* inheriting n_PS_SOURCING state */
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
};

struct a_lex_input_inject{
   struct a_lex_input_inject *lii_next;
   size_t lii_len;
   bool_t lii_commit;
   char lii_dat[n_VFIELD_SIZE(7)];
};

struct a_lex_input{
   struct a_lex_input *li_outer;
   FILE *li_file;                /* File we were in */
   void *li_cond;                /* Saved state of conditional stack */
   ui32_t li_flags;              /* enum a_lex_input_flags */
   ui32_t li_loff;               /* Pseudo (macro): index in .li_lines */
   char **li_lines;              /* Pseudo content, lines unfolded */
   struct a_lex_input_inject *li_inject; /* To be consumed first */
   void (*li_on_finalize)(void *);
   void *li_finalize_arg;
   char li_autorecmem[n_MEMORY_AUTOREC_TYPE_SIZEOF];
   sigjmp_buf li_cmdrec_jmp;     /* TODO one day...  for command_recursive */
   /* SLICE hacks: saved stdin/stdout, saved pstate */
   FILE *li_slice_stdin;
   FILE *li_slice_stdout;
   ui32_t li_slice_psonce;
   ui8_t li_slice__dummy[4];
   char li_name[n_VFIELD_SIZE(0)]; /* Name of file or macro */
};
n_CTA(n_MEMORY_AUTOREC_TYPE_SIZEOF % sizeof(void*) == 0,
   "Inacceptible size of structure buffer");

static sighandler_type a_lex_oldpipe;
static struct a_lex_ghost *a_lex_ghosts;
/* a_lex_cmd_tab[] after fun protos */

/* */
static struct a_lex_input *a_lex_input;

/* For n_source_inject_input(), if a_lex_input==NULL */
static struct a_lex_input_inject *a_lex_input_inject;

static sigjmp_buf a_lex_srbuf; /* TODO GET RID */

/* Isolate the command from the arguments */
static char *a_lex_isolate(char const *comm);

/* `eval' */
static int a_lex_c_eval(void *v);

/* Command ghost handling */
static int a_lex_c_ghost(void *v);
static int a_lex_c_unghost(void *v);

/* */
static char const *a_lex_cmdinfo(struct a_lex_cmd const *lcp);

/* Print a list of all commands */
static int a_lex_c_list(void *v);

static int a_lex__pcmd_cmp(void const *s1, void const *s2);

/* `help' / `?' command */
static int a_lex_c_help(void *v);

/* `exit' and `quit' commands */
static int a_lex_c_exit(void *v);
static int a_lex_c_quit(void *v);

/* Print the binaries version number */
static int a_lex_c_version(void *v);

static int a_lex__version_cmp(void const *s1, void const *s2);

/* n_PS_STATE_PENDMASK requires some actions */
static void a_lex_update_pstate(void);

/* Evaluate a single command.
 * .le_add_history will be updated upon success.
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

/* `source' and `source_if' (if silent_open_error: no pipes allowed, then).
 * Returns FAL0 if file is somehow not usable (unless silent_open_error) or
 * upon evaluation error, and TRU1 on success */
static bool_t a_lex_source_file(char const *file, bool_t silent_open_error);

/* System resource file load()ing or -X command line option array traversal */
static bool_t a_lex_load(struct a_lex_input *lip);

/* A simplified command loop for recursed state machines */
static bool_t a_commands_recursive(enum n_lexinput_flags lif);

/* `read' */
static int a_lex_c_read(void *v);

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
   { "#", NULL, ARG_STRLIST, 0, 0
      DS(N_("Comment command: ignore remaining (continuable) line")) },
   { "-", NULL, ARG_NOLIST, 0, 0
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
a_lex_c_eval(void *v){
   /* TODO HACK! `eval' should be nothing else but a command prefix, evaluate
    * TODO ARGV with shell rules, but if that is not possible then simply
    * TODO adjust argv/argc of "the CmdCtx" that we will have "exec" real cmd */
   struct n_string s_b, *sp;
   si32_t rv;
   size_t i, j;
   char const **argv, *cp;
   NYD_ENTER;

   argv = v;

   for(j = i = 0; (cp = argv[i]) != NULL; ++i)
      j += strlen(cp);

   sp = n_string_creat_auto(&s_b);
   sp = n_string_reserve(sp, j);

   for(i = 0; (cp = argv[i]) != NULL; ++i){
      if(i > 0)
         sp = n_string_push_c(sp, ' ');
      sp = n_string_push_cp(sp, cp);
   }

   /* TODO HACK! We should inherit the current n_lexinput_flags via CmdCtx,
    * TODO for now we don't have such sort of!  n_PS_COMPOSE_MODE is a hack
    * TODO by itself, since ever: misuse the hack for a hack.
    * TODO Further more, exit handling is very grazy */
   (void)/*XXX*/n_source_command((n_pstate & n_PS_COMPOSE_MODE
         ? n_LEXINPUT_CTX_COMPOSE : n_LEXINPUT_CTX_DEFAULT), n_string_cp(sp));
   cp = ok_vlook(__qm);
   if(cp == n_0) /* This is a hack, but since anything is a hack, be hacky */
      rv = 0;
   else if(cp == n_1)
      rv = 1;
   else if(cp == n_m1)
      rv = -1;
   else
      n_idec_si32_cp(&rv, cp, 10, NULL);
   NYD_LEAVE;
   return rv;
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
         fp = n_stdout;

      for(i = 0, gp = a_lex_ghosts; gp != NULL; gp = gp->lg_next)
         fprintf(fp, "wysh ghost %s %s\n",
            gp->lg_name, n_shexp_quote_cp(gp->lg_cmd.s, TRU1));

      if(fp != n_stdout){
         page_or_print(fp, i);
         Fclose(fp);
      }
      goto jleave;
   }

   /* Verify the ghost name is a valid one, and not a command modifier */
   if(*argv[0] == '\0' || *a_lex_isolate(argv[0]) != '\0' ||
         !asccasecmp(argv[0], "ignerr") || !asccasecmp(argv[0], "wysh") ||
         !asccasecmp(argv[0], "vput")){
      n_err(_("`ghost': can't canonicalize %s\n"),
         n_shexp_quote_cp(argv[0], FAL0));
      v = NULL;
      goto jleave;
   }

   /* Show command of single ghost? */
   if(argv[1] == NULL){
      for(gp = a_lex_ghosts; gp != NULL; gp = gp->lg_next)
         if(!strcmp(argv[0], gp->lg_name)){
            fprintf(n_stdout, "wysh ghost %s %s\n",
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
   gp = smalloc(n_VSTRUCT_SIZEOF(struct a_lex_ghost, lg_name) + nl + cl);
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

static char const *
a_lex_cmdinfo(struct a_lex_cmd const *lcp){
   struct n_string rvb, *rv;
   char const *cp;
   NYD2_ENTER;

   rv = n_string_creat_auto(&rvb);
   rv = n_string_reserve(rv, 80);

   switch(lcp->lc_argtype & ARG_ARGMASK){
   case ARG_MSGLIST: cp = N_("message-list"); break;
   case ARG_STRLIST: cp = N_("string data"); break;
   case ARG_RAWLIST: cp = N_("old-style quoting"); break;
   case ARG_NOLIST: cp = N_("no arguments"); break;
   case ARG_NDMLIST: cp = N_("message-list (no default)"); break;
   case ARG_WYSHLIST: cp = N_("sh(1)ell-style quoting"); break;
   default: cp = N_("`wysh' for sh(1)ell-style quoting"); break;
   }
   rv = n_string_push_cp(rv, V_(cp));

   if(lcp->lc_argtype & ARG_V)
      rv = n_string_push_cp(rv, _(" | `vput' modifier"));
   if(lcp->lc_argtype & ARG_EM)
      rv = n_string_push_cp(rv, _(" | status in *!*"));

   if(lcp->lc_argtype & ARG_A)
      rv = n_string_push_cp(rv, _(" | needs box"));
   if(lcp->lc_argtype & ARG_I)
      rv = n_string_push_cp(rv, _(" | only interactive"));
   if(lcp->lc_argtype & ARG_M)
      rv = n_string_push_cp(rv, _(" | send mode"));
   if(lcp->lc_argtype & ARG_R)
      rv = n_string_push_cp(rv, _(" | no compose mode"));
   if(lcp->lc_argtype & ARG_S)
      rv = n_string_push_cp(rv, _(" | after startup"));
   if(lcp->lc_argtype & ARG_X)
      rv = n_string_push_cp(rv, _(" | subprocess"));

   cp = n_string_cp(rv);
   NYD2_LEAVE;
   return cp;
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
      fp = n_stdout;

   fprintf(fp, _("Commands are:\n"));
   l = 1;
   for(i = 0, cursor = cpa; (cp = *cursor++) != NULL;){
      if(cp->lc_func == &c_cmdnotsupp)
         continue;
      if(n_poption & n_PO_D_V){
         fprintf(fp, "%s\n", cp->lc_name);
         ++l;
#ifdef HAVE_DOCSTRINGS
         fprintf(fp, "  : %s\n", V_(cp->lc_doc));
         ++l;
#endif
         fprintf(fp, "  : %s\n", a_lex_cmdinfo(cp));
         ++l;
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

   if(fp != n_stdout){
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
      struct a_lex_cmd const *lcp, *lcpmax;

      /* Ghosts take precedence */
      for(gp = a_lex_ghosts; gp != NULL; gp = gp->lg_next)
         if(!strcmp(arg, gp->lg_name)){
            fprintf(n_stdout, "%s -> ", arg);
            arg = gp->lg_cmd.s;
            break;
         }

      lcpmax = &(lcp = a_lex_cmd_tab)[n_NELEM(a_lex_cmd_tab)];
jredo:
      for(; lcp < lcpmax; ++lcp){
         if(is_prefix(arg, lcp->lc_name)){
            fputs(arg, n_stdout);
            if(strcmp(arg, lcp->lc_name))
               fprintf(n_stdout, " (%s)", lcp->lc_name);
         }else
            continue;

#ifdef HAVE_DOCSTRINGS
         fprintf(n_stdout, ": %s", V_(lcp->lc_doc));
#endif
         if(n_poption & n_PO_D_V)
            fprintf(n_stdout, "\n  : %s", a_lex_cmdinfo(lcp));
         putc('\n', n_stdout);
         rv = 0;
         goto jleave;
      }

      if(PTRCMP(lcpmax, ==, &a_lex_cmd_tab[n_NELEM(a_lex_cmd_tab)])){
         lcpmax = &(lcp =
               a_lex_special_cmd_tab)[n_NELEM(a_lex_special_cmd_tab)];
         goto jredo;
      }

      if(gp != NULL){
         fprintf(n_stdout, "%s\n", n_shexp_quote_cp(arg, TRU1));
         rv = 0;
      }else{
         n_err(_("Unknown command: `%s'\n"), arg);
         rv = 1;
      }
   }else{
      /* Very ugly, but take care for compiler supported string lengths :( */
      fputs(n_progname, n_stdout);
      fputs(_(
         " commands -- <msglist> denotes message specifications,\n"
         "e.g., 1-5, :n or ., separated by spaces:\n"), n_stdout);
      fputs(_(
"\n"
"type <msglist>         type (alias: `print') messages (honour `retain' etc.)\n"
"Type <msglist>         like `type' but always show all headers\n"
"next                   goto and type next message\n"
"from <msglist>         (search and) print header summary for the given list\n"
"headers                header summary for messages surrounding \"dot\"\n"
"delete <msglist>       delete messages (can be `undelete'd)\n"),
         n_stdout);

      fputs(_(
"\n"
"save <msglist> folder  append messages to folder and mark as saved\n"
"copy <msglist> folder  like `save', but don't mark them (`move' moves)\n"
"write <msglist> file   write message contents to file (prompts for parts)\n"
"Reply <msglist>        reply to message senders only\n"
"reply <msglist>        like `Reply', but address all recipients\n"
"Lreply <msglist>       forced mailing-list `reply' (see `mlist')\n"),
         n_stdout);

      fputs(_(
"\n"
"mail <recipients>      compose a mail for the given recipients\n"
"file folder            change to another mailbox\n"
"File folder            like `file', but open readonly\n"
"quit                   quit and apply changes to the current mailbox\n"
"xit or exit            like `quit', but discard changes\n"
"!shell command         shell escape\n"
"list [<anything>]      all available commands [in search order]\n"),
         n_stdout);

      rv = (ferror(n_stdout) != 0);
   }
jleave:
   NYD_LEAVE;
   return rv;
}

static int
a_lex_c_exit(void *v){
   NYD_ENTER;
   n_UNUSED(v);

   if(n_psonce & n_PSO_STARTED){
      /* In recursed state, return error to just pop the input level */
      if(!(n_pstate & n_PS_SOURCING)){
#ifdef n_HAVE_TCAP
         if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_QUICKRUN_MASK))
            n_termcap_destroy();
#endif
         exit(n_EXIT_OK);
      }
   }
   n_pstate |= n_PS_EXIT;
   NYD_LEAVE;
   return 0;
}

static int
a_lex_c_quit(void *v){
   NYD_ENTER;
   n_UNUSED(v);

   /* If we are n_PS_SOURCING, then return 1 so _evaluate() can handle it.
    * Otherwise return -1 to abort command loop */
   n_pstate |= n_PS_EXIT;
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

   fprintf(n_stdout, _("%s version %s\nFeatures included (+) or not (-)\n"),
      n_uagent, ok_vlook(version));

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
      fprintf(n_stdout, "%-*s ", longest, cp);
      i2 += longest;
      if(UICMP(z, ++i2 + longest, >=, n_scrnwidth) || i == 0){
         i2 = 0;
         putc('\n', n_stdout);
      }
   }

   if((rv = ferror(n_stdout) != 0))
      clearerr(n_stdout);
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

   if(n_pstate & n_PS_SIGWINCH_PEND){
      char buf[32];

      snprintf(buf, sizeof buf, "%d", n_scrnwidth);
      ok_vset(COLUMNS, buf);
      snprintf(buf, sizeof buf, "%d", n_scrnheight);
      ok_vset(LINES, buf);
   }

   n_pstate &= ~n_PS_PSTATE_PENDMASK;
   NYD_LEAVE;
}

static int
a_lex_evaluate(struct a_lex_eval_ctx *evp){
   /* xxx old style(9), but also old code */
   struct str line;
   char _wordbuf[2], *arglist[MAXARGC], *cp, *word;
   struct a_lex_ghost *gp;
   struct a_lex_cmd const *cmd;
   int rv, c;
   enum {
      a_NONE = 0,
      a_GHOST_MASK = (1<<3) - 1, /* Ghost recursion counter bits */
      a_NOPREFIX = 1<<4,         /* Modifier prefix not allowed right now */
      a_NOGHOST = 1<<5,          /* No ghost expansion modifier */
      /* New command modifier prefixes must be reflected in a_lex_c_ghost()! */
      a_IGNERR = 1<<6,           /* ignerr modifier prefix */
      a_WYSH = 1<<7,             /* XXX v15+ drop wysh modifier prefix */
      a_VPUT = 1<<8              /* vput modifier prefix */
   } flags;
   NYD_ENTER;

   flags = a_NONE;
   rv = 1;
   cmd = NULL;
   gp = NULL;
   line = evp->le_line; /* XXX don't change original (buffer pointer) */
   assert(line.s[line.l] == '\0');
   evp->le_add_history = FAL0;

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
      goto jerr0;

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

   /* No-expansion modifier? */
   if(!(flags & a_NOPREFIX) && *word == '\\'){
      ++word;
      --c;
      flags |= a_NOGHOST;
   }

   /* It may be a modifier prefix */
   if(c == sizeof("ignerr") -1 && !asccasecmp(word, "ignerr")){
      flags |= a_NOPREFIX | a_IGNERR;
      line.s = cp;
      goto jrestart;
   }else if(c == sizeof("wysh") -1 && !asccasecmp(word, "wysh")){
      flags |= a_NOPREFIX | a_WYSH;
      line.s = cp;
      goto jrestart;
   }else if(c == sizeof("vput") -1 && !asccasecmp(word, "vput")){
      flags |= a_NOPREFIX | a_VPUT;
      line.s = cp;
      goto jrestart;
   }

   /* Look up the command; if not found, bitch.
    * Normally, a blank command would map to the first command in the
    * table; while n_PS_SOURCING, however, we ignore blank lines to eliminate
    * confusion; act just the same for ghosts */
   if(*word == '\0'){
      if((n_pstate & n_PS_ROBOT) || gp != NULL)
         goto jerr0;
      cmd = a_lex_cmd_tab + 0;
      goto jexec;
   }

   if(!(flags & a_NOGHOST) && (flags & a_GHOST_MASK) != a_GHOST_MASK){
      /* TODO relink list head, so it's sorted on usage over time?
       * TODO in fact, there should be one hashmap over all commands and ghosts
       * TODO so that the lookup could be made much more efficient than it is
       * TODO now (two adjacent list searches! */
      ui8_t expcnt;

      expcnt = (flags & a_GHOST_MASK);
      ++expcnt;
      flags = (flags & ~(a_GHOST_MASK | a_NOPREFIX)) | expcnt;

      /* Avoid self-recursion; yes, the user could use \ no-expansion, but.. */
      if(gp != NULL && !strcmp(word, gp->lg_name)){
         if(n_poption & n_PO_D_V)
            n_err(_("Actively avoiding self-recursion of `ghost': %s\n"),
               word);
      }else for(gp = a_lex_ghosts; gp != NULL; gp = gp->lg_next)
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

      if(!(s = condstack_isskip()) || (n_poption & n_PO_D_V))
         n_err(_("Unknown command%s: `%s'\n"),
            (s ? _(" (ignored due to `if' condition)") : n_empty), word);
      if(s)
         goto jerr0;
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
      goto jerr0;

   /* Process the arguments to the command, depending on the type it expects */
   if((cmd->lc_argtype & ARG_I) && !(n_psonce & n_PSO_INTERACTIVE) &&
         !(n_poption & n_PO_BATCH_FLAG)){
      n_err(_("May not execute `%s' unless interactive or in batch mode\n"),
         cmd->lc_name);
      goto jleave;
   }
   if(!(cmd->lc_argtype & ARG_M) && (n_psonce & n_PSO_SENDMODE)){
      n_err(_("May not execute `%s' while sending\n"), cmd->lc_name);
      goto jleave;
   }
   if(cmd->lc_argtype & ARG_R){
      if(n_pstate & n_PS_COMPOSE_MODE){
         /* TODO n_PS_COMPOSE_MODE: should allow `reply': ~:reply! */
         n_err(_("Cannot invoke `%s' when in compose mode\n"), cmd->lc_name);
         goto jleave;
      }
      /* TODO Nothing should prevent ARG_R in conjunction with
       * TODO n_PS_ROBOT|_SOURCING; see a.._may_yield_control()! */
      if(n_pstate & (n_PS_ROBOT | n_PS_SOURCING)){
         n_err(_("Cannot invoke `%s' from a macro or during file inclusion\n"),
            cmd->lc_name);
         goto jleave;
      }
   }
   if((cmd->lc_argtype & ARG_S) && !(n_psonce & n_PSO_STARTED)){
      n_err(_("May not execute `%s' during startup\n"), cmd->lc_name);
      goto jleave;
   }
   if(!(cmd->lc_argtype & ARG_X) && (n_pstate & n_PS_COMPOSE_FORKHOOK)){
      n_err(_("Cannot invoke `%s' from a hook running in a child process\n"),
         cmd->lc_name);
      goto jleave;
   }

   if((cmd->lc_argtype & ARG_A) && mb.mb_type == MB_VOID){
      n_err(_("Cannot execute `%s' without active mailbox\n"), cmd->lc_name);
      goto jleave;
   }
   if((cmd->lc_argtype & ARG_W) && !(mb.mb_perm & MB_DELE)){
      n_err(_("May not execute `%s' -- message file is read only\n"),
         cmd->lc_name);
      goto jleave;
   }

   if(cmd->lc_argtype & ARG_O)
      n_OBSOLETE2(_("this command will be removed"), cmd->lc_name);

   if((flags & a_WYSH) && (cmd->lc_argtype & ARG_ARGMASK) != ARG_WYRALIST){
      n_err(_("`wysh' prefix doesn't affect `%s'\n"), cmd->lc_name);
      flags &= ~a_WYSH;
   }
   if((flags & a_VPUT) && !(cmd->lc_argtype & ARG_V)){
      n_err(_("`vput' prefix doesn't affect `%s'\n"), cmd->lc_name);
      flags &= ~a_VPUT;
   }

   /* TODO v15: strip n_PS_ARGLIST_MASK off, just in case the actual command
    * TODO doesn't use any of those list commands which strip this mask,
    * TODO and for now we misuse bits for checking relation to history;
    * TODO argument state should be property of a per-command carrier instead */
   n_pstate &= ~n_PS_ARGLIST_MASK;
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
         if(!(n_pstate & n_PS_HOOK_MASK))
            fprintf(n_stdout, _("No applicable messages\n"));
         break;
      }
      rv = (*cmd->lc_func)(n_msgvec);
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
      rv = (*cmd->lc_func)(n_msgvec);
      break;

   case ARG_STRLIST:
      /* Just the straight string, with leading blanks removed */
      while(whitechar(*cp))
         ++cp;
      rv = (*cmd->lc_func)(cp);
      break;

   case ARG_WYSHLIST:
      c = 1;
      if(0){
         /* FALLTHRU */
   case ARG_WYRALIST:
         c = (flags & a_WYSH) ? 1 : 0;
         if(0){
   case ARG_RAWLIST:
            c = 0;
         }
      }
      if((c = getrawlist((c != 0), arglist, n_NELEM(arglist), cp, line.l)) < 0){
         n_err(_("Invalid argument list\n"));
         break;
      }
      c -= ((flags & a_VPUT) != 0); /* XXX c=int */

      if(c < cmd->lc_minargs){
         n_err(_("`%s' requires at least %u arg(s)\n"),
            cmd->lc_name, (ui32_t)cmd->lc_minargs + ((flags & a_VPUT) != 0));
         break;
      }
#undef lc_minargs
      if(c > cmd->lc_maxargs){
         n_err(_("`%s' takes no more than %u arg(s)\n"),
            cmd->lc_name, (ui32_t)cmd->lc_maxargs + ((flags & a_VPUT) != 0));
         break;
      }
#undef lc_maxargs

      if(flags & a_VPUT){
         char const *emsg;

         if(!n_shexp_is_valid_varname(arglist[0]))
            emsg = N_("not a valid variable name");
         else if(!n_var_is_user_writable(arglist[0]))
            emsg = N_("either not a user writable, or a boolean variable");
         else
            emsg = NULL;
         if(emsg != NULL){
            n_err(_("`%s': %s: %s\n"),
                  cmd->lc_name, V_(emsg), n_shexp_quote_cp(arglist[0], FAL0));
            break;
         }

         ++c;
         n_pstate |= n_PS_ARGMOD_VPUT;
      }
      rv = (*cmd->lc_func)(arglist);
      break;

   case ARG_NOLIST:
      /* Just the constant zero, for exiting, eg. */
      rv = (*cmd->lc_func)(0);
      break;

   default:
      DBG( n_panic(_("Implementation error: unknown argument type: %d"),
         cmd->lc_argtype & ARG_ARGMASK); )
      goto jerr0;
   }

   if(!(cmd->lc_argtype & ARG_H))
      evp->le_add_history = (((cmd->lc_argtype & ARG_G) ||
            (n_pstate & n_PS_MSGLIST_GABBY)) ? TRUM1 : TRU1);

jleave:
   n_PS_ROOT_BLOCK(ok_vset(__qm, (rv == 0 ? n_0 : n_1))); /* TODO num=1/real */

   if(flags & a_IGNERR){
      rv = 0;
      n_exit_status = n_EXIT_OK;
   }

   /* Exit the current source file on error TODO what a mess! */
   if(rv == 0)
      n_pstate &= ~n_PS_EVAL_ERROR;
   else{
      n_pstate |= n_PS_EVAL_ERROR;
      if(rv < 0 || (n_pstate & n_PS_ROBOT)){ /* FIXME */
         rv = 1;
         goto jret;
      }
      goto jret0;
   }

   if(cmd == NULL)
      goto jret0;
   if((cmd->lc_argtype & ARG_P) && ok_blook(autoprint)) /* TODO rid of that! */
      if(visible(dot))
         n_source_inject_input(n_INPUT_INJECT_COMMIT, "\\type",
            sizeof("\\type") -1);

   if(!(n_pstate & (n_PS_SOURCING | n_PS_HOOK_MASK)) &&
         !(cmd->lc_argtype & ARG_T))
      n_pstate |= n_PS_SAW_COMMAND;
jleave0:
   n_pstate &= ~n_PS_EVAL_ERROR;
jret0:
   rv = 0;
jret:
   NYD_LEAVE;
   return rv;
jerr0:
   n_PS_ROOT_BLOCK(ok_vset(__qm, n_0)); /* TODO num=1/real */
   goto jleave0;
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
   exit(n_EXIT_ERR);
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
   siglongjmp(a_lex_srbuf, 0); /* FIXME get rid */
}

static void
a_lex_unstack(bool_t eval_error){
   struct a_lex_input *lip;
   NYD_ENTER;

   /* Free input injections of this level first */
   /* C99 */{
      struct a_lex_input_inject **liipp, *liip;

      if((lip = a_lex_input) == NULL)
         liipp = &a_lex_input_inject;
      else
         liipp = &lip->li_inject;

      while((liip = *liipp) != NULL){
         *liipp = liip->lii_next;
         free(liip);
      }

      if(lip == NULL || !(lip->li_flags & a_LEX_SLICE))
         n_memory_reset();
   }

   if(lip == NULL){
      /* If called from a_lex_onintr(), be silent FIXME */
      n_pstate &= ~(n_PS_SOURCING | n_PS_ROBOT);
      if(eval_error == TRUM1 || !(n_psonce & n_PSO_STARTED))
         goto jleave;
      goto jerr;
   }

   if(lip->li_flags & a_LEX_SLICE){ /* TODO Temporary hack */
      n_stdin = lip->li_slice_stdin;
      n_stdout = lip->li_slice_stdout;
      n_psonce = lip->li_slice_psonce;
      goto jthe_slice_hack;
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

jthe_slice_hack:
   if(lip->li_on_finalize != NULL)
      (*lip->li_on_finalize)(lip->li_finalize_arg);

   if((a_lex_input = lip->li_outer) == NULL){
      n_pstate &= ~(n_PS_SOURCING | n_PS_ROBOT);
   }else{
      if((a_lex_input->li_flags & (a_LEX_MACRO | a_LEX_SUPER_MACRO)) ==
            (a_LEX_MACRO | a_LEX_SUPER_MACRO))
         n_pstate &= ~n_PS_SOURCING;
      assert(n_pstate & n_PS_ROBOT);
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
      /* POSIX says
       *    Any errors in the start-up file shall either cause mailx to
       *    terminate with a diagnostic message and a non-zero status or to
       *    continue after writing a diagnostic message, ignoring the
       *    remainder of the lines in the start-up file
       * But print the diagnostic only for the outermost resource unless the
       * user is debugging or in verbose mode */
      if((n_poption & n_PO_D_V) ||
            (!(n_psonce & n_PSO_STARTED) &&
             !(lip->li_flags & (a_LEX_SLICE | a_LEX_MACRO)) &&
             lip->li_outer == NULL))
         n_alert(_("Stopped %s %s due to errors%s"),
            (n_psonce & n_PSO_STARTED
             ? (lip->li_flags & a_LEX_SLICE ? _("sliced in program")
             : (lip->li_flags & a_LEX_MACRO
                ? (lip->li_flags & a_LEX_MACRO_CMD
                   ? _("evaluating command") : _("evaluating macro"))
                : (lip->li_flags & a_LEX_PIPE
                   ? _("executing `source'd pipe")
                   : _("loading `source'd file")))
             )
             : (lip->li_flags & a_LEX_MACRO
                ? (lip->li_flags & a_LEX_MACRO_X_OPTION
                   ? _("evaluating command line") : _("evaluating macro"))
                : _("loading initialization resource"))),
            lip->li_name,
            (n_poption & n_PO_DEBUG
               ? n_empty : _(" (enable *debug* for trace)")));
   }

   if(!(n_psonce & (n_PSO_INTERACTIVE | n_PSO_STARTED))){
      if(n_poption & n_PO_D_V)
         n_alert(_("Non-interactive, bailing out due to errors "
            "in startup load phase"));
      exit(n_EXIT_ERR);
   }
   goto jleave;
}

static bool_t
a_lex_source_file(char const *file, bool_t silent_open_error){
   struct a_lex_input *lip;
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
   ((ispipe = (!silent_open_error && (nlen = strlen(file)) > 0 &&
         file[--nlen] == '|')))
#else
   ispipe = FAL0;
   if(!silent_open_error)
      for(nlen = strlen(file); nlen > 0;){
         char c;

         c = file[--nlen];
         if(!blankchar(c)){
            if(c == '|'){
               nbuf = savestrbuf(file, nlen);
               ispipe = TRU1;
            }
            break;
         }
      }
#endif

   if(ispipe){
      if((fip = Popen(nbuf /* #if 0 above = savestrbuf(file, nlen)*/, "r",
            ok_vlook(SHELL), NULL, COMMAND_FD_NULL)) == NULL)
         goto jeopencheck;
   }else if((nbuf = fexpand(file, FEXP_LOCAL)) == NULL)
      goto jeopencheck;
   else if((fip = Fopen(nbuf, "r")) == NULL){
jeopencheck:
      if(!silent_open_error || (n_poption & n_PO_D_V))
         n_perr(nbuf, 0);
      if(silent_open_error)
         fip = (FILE*)-1;
      goto jleave;
   }

   lip = smalloc(n_VSTRUCT_SIZEOF(struct a_lex_input, li_name) +
         (nlen = strlen(nbuf) +1));
   memset(lip, 0, n_VSTRUCT_SIZEOF(struct a_lex_input, li_name));
   lip->li_outer = a_lex_input;
   lip->li_file = fip;
   lip->li_cond = condstack_release();
   n_memory_autorec_push(&lip->li_autorecmem[0]);
   lip->li_flags = (ispipe ? a_LEX_FREE | a_LEX_PIPE : a_LEX_FREE) |
         (a_lex_input != NULL && (a_lex_input->li_flags & a_LEX_SUPER_MACRO)
          ? a_LEX_SUPER_MACRO : 0);
   memcpy(lip->li_name, nbuf, nlen);

   n_pstate |= n_PS_SOURCING | n_PS_ROBOT;
   a_lex_input = lip;
   if(!a_commands_recursive(n_LEXINPUT_NONE | n_LEXINPUT_NL_ESC))
      fip = NULL;
jleave:
   NYD_LEAVE;
   return (fip != NULL);
}

static bool_t
a_lex_load(struct a_lex_input *lip){
   bool_t rv;
   NYD2_ENTER;

   assert(!(n_psonce & n_PSO_STARTED));
   assert(a_lex_input == NULL);

   /* POSIX:
    *    Any errors in the start-up file shall either cause mailx to terminate
    *    with a diagnostic message and a non-zero status or to continue after
    *    writing a diagnostic message, ignoring the remainder of the lines in
    *    the start-up file. */
   lip->li_cond = condstack_release();
   n_memory_autorec_push(&lip->li_autorecmem[0]);

/* FIXME won't work for now (n_PS_ROBOT needs n_PS_SOURCING sofar)
   n_pstate |= n_PS_ROBOT |
         (lip->li_flags & a_LEX_MACRO_X_OPTION ? 0 : n_PS_SOURCING);
*/
   n_pstate |= n_PS_ROBOT | n_PS_SOURCING;
   if(n_poption & n_PO_D_V)
      n_err(_("Loading %s\n"), n_shexp_quote_cp(lip->li_name, FAL0));
   a_lex_input = lip;
   if(!(rv = n_commands())){
      if(!(n_psonce & n_PSO_INTERACTIVE)){
         if(n_poption & n_PO_D_V)
            n_alert(_("Non-interactive program mode, forced exit"));
         exit(n_EXIT_ERR);
      }else if(n_poption & n_PO_BATCH_FLAG){
         char const *beoe;

         if((beoe = ok_vlook(batch_exit_on_error)) != NULL && *beoe == '1')
            n_pstate |= n_PS_EXIT;
      }
   }
   /* n_PS_EXIT handled by callers */
   NYD2_LEAVE;
   return rv;
}

static void
a_lex__cmdrecint(int sig){ /* TODO one day, we don't need it no more */
   NYD_X; /* Signal handler */
   n_UNUSED(sig);
   siglongjmp(a_lex_input->li_cmdrec_jmp, 1);
}

static bool_t
a_commands_recursive(enum n_lexinput_flags lif){
   volatile int hadint; /* TODO get rid of shitty signal stuff (see signal.c) */
   sighandler_type soldhdl;
   sigset_t sintset, soldset;
   struct a_lex_eval_ctx ev;
   bool_t rv, ever;
   NYD2_ENTER;

   memset(&ev, 0, sizeof ev);

   sigfillset(&sintset);
   sigprocmask(SIG_BLOCK, &sintset, &soldset);
   hadint = FAL0;
   if((soldhdl = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN){
      safe_signal(SIGINT, &a_lex__cmdrecint);
      if(sigsetjmp(a_lex_input->li_cmdrec_jmp, 1)){
         hadint = TRU1;
         goto jjump;
      }
   }
   sigprocmask(SIG_SETMASK, &soldset, NULL);

   n_COLOUR( n_colour_env_push(); )
   rv = TRU1;
   for(ever = FAL0;; ever = TRU1){
      char const *beoe;
      int n;

      if(ever)
         n_memory_reset();

      /* Read a line of commands and handle end of file specially */
      ev.le_line.l = ev.le_line_size;
      n = n_lex_input(lif, NULL, &ev.le_line.s, &ev.le_line.l, NULL);
      ev.le_line_size = (ui32_t)ev.le_line.l;
      ev.le_line.l = (ui32_t)n;

      if(n < 0)
         break;

      n = a_lex_evaluate(&ev);
      beoe = (n_poption & n_PO_BATCH_FLAG)
            ? ok_vlook(batch_exit_on_error) : NULL;

      if(n){
         if(beoe != NULL && *beoe == '1'){
            if(n_exit_status == n_EXIT_OK)
               n_exit_status = n_EXIT_ERR;
         }
         rv = FAL0;
         break;
      }
      if(beoe != NULL){
         if(n_exit_status != n_EXIT_OK)
            break;
      }
   }
jjump: /* TODO */
   a_lex_unstack(!rv);
   n_COLOUR( n_colour_env_pop(FAL0); )

   if(ev.le_line.s != NULL)
      free(ev.le_line.s);

   if(soldhdl != SIG_IGN)
      safe_signal(SIGINT, soldhdl);
   NYD2_LEAVE;
   if(hadint){
      sigprocmask(SIG_SETMASK, &soldset, NULL);
      n_raise(SIGINT);
   }
   return rv;
}

static int
a_lex_c_read(void *v){ /* TODO IFS? how? -r */
   char const **argv, *cp, *emv, *cp2;
   int rv;
   NYD2_ENTER;

   rv = 0;
   for(argv = v; (cp = *argv++) != NULL;)
      if(!n_shexp_is_valid_varname(cp) || !n_var_is_user_writable(cp)){
         n_err(_("`read': variable (name) cannot be used: %s\n"),
            n_shexp_quote_cp(cp, FAL0));
         rv = 1;
      }
   if(rv)
      goto jleave;

   emv = n_0;

   cp = n_lex_input_cp(((n_pstate & n_PS_COMPOSE_MODE
            ? n_LEXINPUT_CTX_COMPOSE : n_LEXINPUT_CTX_DEFAULT) |
         n_LEXINPUT_FORCE_STDIN | n_LEXINPUT_NL_ESC |
         n_LEXINPUT_PROMPT_NONE /* XXX POSIX: PS2: yes! */),
         NULL, NULL);
   if(cp == NULL)
      cp = n_empty;

   for(argv = v; *argv != NULL; ++argv){
      char c;

      while(blankchar(*cp))
         ++cp;
      if(*cp == '\0')
         break;

      /* The last variable gets the remaining line less trailing IFS */
      if(argv[1] == NULL){
         for(cp2 = cp; *cp2 != '\0'; ++cp2)
            ;
         for(; cp2 > cp; --cp2){
            c = cp2[-1];
            if(!blankchar(c))
               break;
         }
      }else
         for(cp2 = cp; (c = *++cp2) != '\0';)
            if(blankchar(c))
               break;

      /* C99 xxx This is a CC warning workaround (-Wbad-function-cast) */{
         char *vcp;

         vcp = savestrbuf(cp, PTR2SIZE(cp2 - cp));
         if(!n_var_vset(*argv, (uintptr_t)vcp))
            emv = n_1;
      }

      cp = cp2;
   }

   /* Set the remains to the empty string */
   for(; *argv != NULL; ++argv)
      if(!n_var_vset(*argv, (uintptr_t)n_empty))
         emv = n_1;

   n__EM_SET(emv);
   rv = 0;
jleave:
   NYD2_LEAVE;
   return rv;
}

FL int
c_cmdnotsupp(void *vp){
   NYD_ENTER;
   n_UNUSED(vp);
   n_err(_("The requested feature is not compiled in\n"));
   NYD_LEAVE;
   return 1;
}

FL bool_t
n_commands(void){ /* FIXME */
   struct a_lex_eval_ctx ev;
   int n;
   bool_t volatile rv;
   NYD_ENTER;

   rv = TRU1;

   if (!(n_pstate & n_PS_SOURCING)) {
      if (safe_signal(SIGINT, SIG_IGN) != SIG_IGN)
         safe_signal(SIGINT, &a_lex_onintr);
      if (safe_signal(SIGHUP, SIG_IGN) != SIG_IGN)
         safe_signal(SIGHUP, &a_lex_hangup);
   }
   a_lex_oldpipe = safe_signal(SIGPIPE, SIG_IGN);
   safe_signal(SIGPIPE, a_lex_oldpipe);

   memset(&ev, 0, sizeof ev);

   (void)sigsetjmp(a_lex_srbuf, 1); /* FIXME get rid */
   for (;;) {
      n_COLOUR( n_colour_env_pop(TRU1); )

      /* TODO Unless we have our signal manager (or however we do it) child
       * TODO processes may have time slots where their execution isn't
       * TODO protected by signal handlers (in between start and setup
       * TODO completed).  close_all_files() is only called from onintr()
       * TODO so those may linger possibly forever */
      if(!(n_pstate & n_PS_SOURCING))
         close_all_files();

      interrupts = 0;

      n_memory_reset();

      if (!(n_pstate & n_PS_SOURCING)) {
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

      if (!(n_pstate & n_PS_SOURCING) && (n_psonce & n_PSO_INTERACTIVE)) {
         char *cp;

         if ((cp = ok_vlook(newmail)) != NULL) {
            struct stat st;

/* FIXME TEST WITH NOPOLL ETC. !!! */
            n = (cp != NULL && strcmp(cp, "nopoll"));
            if ((mb.mb_type == MB_FILE && !stat(mailname, &st) &&
                     st.st_size > mailsize) ||
                  (mb.mb_type == MB_MAILDIR && n != 0)) {
               size_t odot = PTR2SIZE(dot - message);
               ui32_t odid = (n_pstate & n_PS_DID_PRINT_DOT);

               if (setfile(mailname,
                     FEDIT_NEWMAIL |
                     ((mb.mb_perm & MB_DELE) ? 0 : FEDIT_RDONLY)) < 0) {
                  n_exit_status |= n_EXIT_ERR;
                  rv = FAL0;
                  break;
               }
               dot = message + odot;
               n_pstate |= odid;
            }
         }

         n_exit_status = n_EXIT_OK;
      }

      /* Read a line of commands and handle end of file specially */
      ev.le_line.l = ev.le_line_size;
      n = n_lex_input(n_LEXINPUT_CTX_DEFAULT | n_LEXINPUT_NL_ESC, NULL,
            &ev.le_line.s, &ev.le_line.l, NULL);
      ev.le_line_size = (ui32_t)ev.le_line.l;
      ev.le_line.l = (ui32_t)n;

      if (n < 0) {
/* FIXME did unstack() when n_PS_SOURCING, only break with n_PS_LOADING */
         if (!(n_pstate & n_PS_ROBOT) &&
               (n_psonce & n_PSO_INTERACTIVE) && ok_blook(ignoreeof)) {
            fprintf(n_stdout, _("*ignoreeof* set, use `quit' to quit.\n"));
            n_msleep(500, FAL0);
            continue;
         }
         break;
      }

      n_pstate &= ~n_PS_HOOK_MASK;
      /* C99 */{
         char const *beoe;
         int estat;

         estat = a_lex_evaluate(&ev);
         beoe = (n_poption & n_PO_BATCH_FLAG)
               ? ok_vlook(batch_exit_on_error) : NULL;

         if(estat){
            if(beoe != NULL && *beoe == '1'){
               if(n_exit_status == n_EXIT_OK)
                  n_exit_status = n_EXIT_ERR;
               rv = FAL0;
               break;
            }
            if(!(n_psonce & n_PSO_STARTED)){ /* TODO join n_PS_EVAL_ERROR */
               if(a_lex_input == NULL ||
                     !(a_lex_input->li_flags & a_LEX_MACRO_X_OPTION)){
                  rv = FAL0;
                  break;
               }
            }else
               break;
         }

         if(beoe != NULL){
            if(n_exit_status != n_EXIT_OK)
               break;
            /* TODO n_PS_EVAL_ERROR and n_PS_SOURCING!  Sigh!! */
            if((n_pstate & (n_PS_SOURCING | n_PS_EVAL_ERROR)
                  ) == n_PS_EVAL_ERROR){
               n_exit_status = n_EXIT_ERR;
               break;
            }
         }
      }

      if(!(n_pstate & n_PS_SOURCING) && (n_psonce & n_PSO_INTERACTIVE) &&
            ev.le_add_history)
         n_tty_addhist(ev.le_line.s, (ev.le_add_history != TRU1));

      if(n_pstate & n_PS_EXIT)
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
   int nold, n;
   NYD2_ENTER;

   if(a_lex_input != NULL && (a_lex_input->li_flags & a_LEX_FORCE_EOF)){
      n = -1;
      goto jleave;
   }

   /* Special case macro mode: never need to prompt, lines have always been
    * unfolded already */
   if(!(lif & n_LEXINPUT_FORCE_STDIN) &&
         a_lex_input != NULL && (a_lex_input->li_flags & a_LEX_MACRO)){
      struct a_lex_input_inject *liip;

      if(*linebuf != NULL)
         free(*linebuf);

      /* Injection in progress?  Don't care about the autocommit state here */
      if((liip = a_lex_input->li_inject) != NULL){
         a_lex_input->li_inject = liip->lii_next;

         *linesize = liip->lii_len;
         *linebuf = (char*)liip;
         memcpy(*linebuf, liip->lii_dat, liip->lii_len +1);
         iftype = "INJECTION";
      }else{
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
               ? "-X OPTION"
               : (a_lex_input->li_flags & a_LEX_MACRO_CMD) ? "CMD" : "MACRO";
      }
      n = (int)*linesize;
      n_pstate |= n_PS_READLINE_NL;
      goto jhave_dat;
   }

   /* Injection in progress? */
   if(!(lif & n_LEXINPUT_FORCE_STDIN)){
      struct a_lex_input_inject **liipp, *liip;

      liipp = (a_lex_input == NULL) ? &a_lex_input_inject
            : &a_lex_input->li_inject;

      if((liip = *liipp) != NULL){
         *liipp = liip->lii_next;

         if(liip->lii_commit){
            if(*linebuf != NULL)
               free(*linebuf);

            /* Simply reuse the buffer */
            n = (int)(*linesize = liip->lii_len);
            *linebuf = (char*)liip;
            memcpy(*linebuf, liip->lii_dat, liip->lii_len +1);
            iftype = "INJECTION";
            n_pstate |= n_PS_READLINE_NL;
            goto jhave_dat;
         }else{
            string = savestrbuf(liip->lii_dat, liip->lii_len);
            free(liip);
         }
      }
   }

   n_pstate &= ~n_PS_READLINE_NL;
   iftype = (!(n_psonce & n_PSO_STARTED) ? "LOAD"
          : (n_pstate & n_PS_SOURCING) ? "SOURCE" : "READ");
   doprompt = ((n_psonce & (n_PSO_INTERACTIVE | n_PSO_STARTED)) ==
         (n_PSO_INTERACTIVE | n_PSO_STARTED) && !(n_pstate & n_PS_ROBOT));
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
      fflush(n_stdout);

   ifile = (a_lex_input != NULL) ? a_lex_input->li_file : n_stdin;
   if(ifile == NULL){
      assert((n_pstate & n_PS_COMPOSE_FORKHOOK) &&
         a_lex_input != NULL && (a_lex_input->li_flags & a_LEX_MACRO));
      ifile = n_stdin;
   }

   for(nold = n = 0;;){
      if(dotty){
         assert(ifile == n_stdin);
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
               fwrite(xprompt.s_dat, 1, xprompt.s_len, n_stdout);
               fflush(n_stdout);
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
            n_pstate |= n_PS_READLINE_NL;
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

   if(n_poption & n_PO_D_VV)
      n_err(_("%s %d bytes <%s>\n"), iftype, n, *linebuf);
jleave:
   if (n_pstate & n_PS_PSTATE_PENDMASK)
      a_lex_update_pstate();

   /* TODO We need to special case a_LEX_SLICE, since that is not managed by us
    * TODO but only established from the outside and we need to drop this
    * TODO overlay context somehow */
   if(n < 0 && a_lex_input != NULL && (a_lex_input->li_flags & a_LEX_SLICE))
      a_lex_unstack(FAL0);
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
         (lif & n_LEXINPUT_HIST_ADD) && (n_psonce & n_PSO_INTERACTIVE))
      n_tty_addhist(rv, ((lif & n_LEXINPUT_HIST_GABBY) != 0));

   if(linebuf != NULL)
      free(linebuf);
   NYD2_LEAVE;
   return rv;
}

FL void
n_load(char const *name){
   struct a_lex_input *lip;
   size_t i;
   FILE *fip;
   NYD_ENTER;

   if(name == NULL || *name == '\0' || (fip = Fopen(name, "r")) == NULL)
      goto jleave;

   i = strlen(name) +1;
   lip = smalloc(n_VSTRUCT_SIZEOF(struct a_lex_input, li_name) + i);
   memset(lip, 0, n_VSTRUCT_SIZEOF(struct a_lex_input, li_name));
   lip->li_file = fip;
   lip->li_flags = a_LEX_FREE;
   memcpy(lip->li_name, name, i);

   a_lex_load(lip);
   n_pstate &= ~n_PS_EXIT;
jleave:
   NYD_LEAVE;
}

FL void
n_load_Xargs(char const **lines, size_t cnt){
   static char const name[] = "-X";

   ui8_t buf[sizeof(struct a_lex_input) + sizeof name];
   char const *srcp, *xsrcp;
   char *cp;
   size_t imax, i, len;
   struct a_lex_input *lip;
   NYD_ENTER;

   lip = (void*)buf;
   memset(lip, 0, n_VSTRUCT_SIZEOF(struct a_lex_input, li_name));
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
   if(n_pstate & n_PS_EXIT)
      exit(n_exit_status);
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

   rv = (a_lex_source_file(*(char**)v, TRU1) == TRU1) ? 0 : 1;
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_source_macro(enum n_lexinput_flags lif, char const *name, char **lines,
      void (*on_finalize)(void*), void *finalize_arg){
   struct a_lex_input *lip;
   size_t i;
   int rv;
   NYD_ENTER;

   lip = smalloc(n_VSTRUCT_SIZEOF(struct a_lex_input, li_name) +
         (i = strlen(name) +1));
   memset(lip, 0, n_VSTRUCT_SIZEOF(struct a_lex_input, li_name));
   lip->li_outer = a_lex_input;
   lip->li_file = NULL;
   lip->li_cond = condstack_release();
   n_memory_autorec_push(&lip->li_autorecmem[0]);
   lip->li_flags = a_LEX_FREE | a_LEX_MACRO | a_LEX_MACRO_FREE_DATA |
         (a_lex_input == NULL || (a_lex_input->li_flags & a_LEX_SUPER_MACRO)
          ? a_LEX_SUPER_MACRO : 0);
   lip->li_lines = lines;
   lip->li_on_finalize = on_finalize;
   lip->li_finalize_arg = finalize_arg;
   memcpy(lip->li_name, name, i);

   n_pstate |= n_PS_ROBOT;
   a_lex_input = lip;
   rv = a_commands_recursive(lif);
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_source_command(enum n_lexinput_flags lif, char const *cmd){
   struct a_lex_input *lip;
   size_t i, ial;
   bool_t rv;
   NYD_ENTER;

   i = strlen(cmd) +1;
   ial = n_ALIGN(i);

   lip = smalloc(n_VSTRUCT_SIZEOF(struct a_lex_input, li_name) +
         ial + 2*sizeof(char*));
   memset(lip, 0, n_VSTRUCT_SIZEOF(struct a_lex_input, li_name));
   lip->li_outer = a_lex_input;
   lip->li_cond = condstack_release();
   n_memory_autorec_push(&lip->li_autorecmem[0]);
   lip->li_flags = a_LEX_FREE | a_LEX_MACRO | a_LEX_MACRO_CMD |
         (a_lex_input == NULL || (a_lex_input->li_flags & a_LEX_SUPER_MACRO)
          ? a_LEX_SUPER_MACRO : 0);
   lip->li_lines = (void*)&lip->li_name[ial];
   memcpy(lip->li_lines[0] = &lip->li_name[0], cmd, i);
   lip->li_lines[1] = NULL;

   n_pstate |= n_PS_ROBOT;
   a_lex_input = lip;
   rv = a_commands_recursive(lif);
   NYD_LEAVE;
   return rv;
}

FL void
n_source_slice_hack(char const *cmd, FILE *new_stdin, FILE *new_stdout,
      ui32_t new_psonce, void (*on_finalize)(void*), void *finalize_arg){
   struct a_lex_input *lip;
   size_t i;
   NYD_ENTER;

   lip = smalloc(n_VSTRUCT_SIZEOF(struct a_lex_input, li_name) +
         (i = strlen(cmd) +1));
   memset(lip, 0, n_VSTRUCT_SIZEOF(struct a_lex_input, li_name));
   lip->li_outer = a_lex_input;
   lip->li_file = new_stdin;
   lip->li_flags = a_LEX_FREE | a_LEX_SLICE;
   lip->li_on_finalize = on_finalize;
   lip->li_finalize_arg = finalize_arg;
   lip->li_slice_stdin = n_stdin;
   lip->li_slice_stdout = n_stdout;
   lip->li_slice_psonce = n_psonce;
   memcpy(lip->li_name, cmd, i);

   n_stdin = new_stdin;
   n_stdout = new_stdout;
   n_psonce = new_psonce;
   n_pstate |= n_PS_ROBOT;
   a_lex_input = lip;
   NYD_LEAVE;
}

FL void
n_source_slice_hack_remove_after_jump(void){
   a_lex_unstack(FAL0);
}

FL bool_t
n_source_may_yield_control(void){ /* TODO this is a terrible hack */
   /* TODO This is obviously hacky in that it depends on _input_stack not
    * TODO loosing any flags when creating new contexts...  Maybe this
    * TODO function should instead walk all up the context stack when
    * TODO there is one, and verify neither level prevents yielding! */
   struct a_lex_input *lip;
   bool_t rv;
   NYD2_ENTER;

   rv = FAL0;

   /* Only when interactive and startup completed */
   if((n_psonce & (n_PSO_INTERACTIVE | n_PSO_STARTED)) !=
         (n_PSO_INTERACTIVE | n_PSO_STARTED))
      goto jleave;

   /* Not when running any hook */
   if(n_pstate & n_PS_HOOK_MASK)
      goto jleave;

   /* Traverse up the stack:
    * . not when controlled by a child process
    * TODO . not when there are pipes involved, we neither handle job control,
    * TODO   nor process groups, that is, controlling terminal acceptably
    * . not when sourcing a file */
   for(lip = a_lex_input; lip != NULL; lip = lip->li_outer){
      ui32_t f;

      if((f = lip->li_flags) & (a_LEX_PIPE | a_LEX_SLICE))
         goto jleave;
      if(!(f & a_LEX_MACRO))
         goto jleave;
   }

   rv = TRU1;
jleave:
   NYD2_LEAVE;
   return rv;
}

FL void
n_source_inject_input(enum n_input_inject_flags iif, char const *buf,
      size_t len){
   NYD_ENTER;
   if(len == UIZ_MAX)
      len = strlen(buf);

   if(UIZ_MAX - n_VSTRUCT_SIZEOF(struct a_lex_input_inject, lii_dat) -1 > len &&
         len > 0){
      size_t i;
      struct a_lex_input_inject *liip,  **liipp;

      liip = n_alloc(n_VSTRUCT_SIZEOF(struct a_lex_input_inject, lii_dat
            ) + 1 + len +1);
      liipp = (a_lex_input == NULL) ? &a_lex_input_inject
            : &a_lex_input->li_inject;
      liip->lii_next = *liipp;
      liip->lii_commit = ((iif & n_INPUT_INJECT_COMMIT) != 0);
      if(buf[i = 0] != ' ' && !(iif & n_INPUT_INJECT_HISTORY))
         liip->lii_dat[i++] = ' '; /* TODO prim. hack to avoid history put! */
      memcpy(&liip->lii_dat[i], buf, len);
      i += len;
      liip->lii_dat[liip->lii_len = i] = '\0';
      *liipp = liip;
   }
   NYD_LEAVE;
}

FL void
n_source_force_eof(void){
   NYD_ENTER;
   assert(a_lex_input != NULL);
   a_lex_input->li_flags |= a_LEX_FORCE_EOF;
   NYD_LEAVE;
}

/* s-it-mode */
