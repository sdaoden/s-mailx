/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Program input of all sorts, input lexing, event loops, command evaluation.
 *@ TODO n_PS_ROBOT requires yet n_PS_SOURCING, which REALLY sucks.
 *@ TODO Commands and ghosts deserve a tree.  Or so.
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
#define n_FILE go

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

enum a_go_flags{
   a_GO_NONE,
   a_GO_FREE = 1u<<0,         /* Structure was allocated, n_free() it */
   a_GO_PIPE = 1u<<1,         /* Open on a pipe */
   a_GO_FILE = 1u<<2,         /* Loading or sourcing a file */
   a_GO_MACRO = 1u<<3,        /* Running a macro */
   a_GO_MACRO_FREE_DATA = 1u<<4, /* Lines are allocated, n_free() once done */
   /* TODO For simplicity this is yet _MACRO plus specialization overlay
    * TODO (_X_OPTION, _CMD) -- these should be types on their own! */
   a_GO_MACRO_X_OPTION = 1u<<5, /* Macro indeed command line -X option */
   a_GO_MACRO_CMD = 1u<<6,    /* Macro indeed single-line: ~:COMMAND */
   /* TODO a_GO_SPLICE: the right way to support *on-compose-splice(-shell)?*
    * TODO would be a command_loop object that emits an on_read_line event, and
    * TODO have a special handler for the compose mode; with that, then,
    * TODO _event_loop() would not call _evaluate() but CTX->on_read_line,
    * TODO and _evaluate() would be the standard impl.,
    * TODO whereas the COMMAND ESCAPE switch in collect.c would be another one.
    * TODO With this generic accmacvar.c:temporary_compose_mode_hook_call()
    * TODO could be dropped, and n_go_macro() could become extended,
    * TODO and/or we would add a n_go_anything(), which would allow special
    * TODO input handlers, special I/O input and output, special `localopts'
    * TODO etc., to be glued to the new execution context.  And all I/O all
    * TODO over this software should not use stdin/stdout, but CTX->in/out.
    * TODO The pstate must be a property of the current execution context, too.
    * TODO This not today. :(  For now we invent a special SPLICE execution
    * TODO context overlay that at least allows to temporarily modify the
    * TODO global pstate, and the global stdin and stdout pointers.  HACK!
    * TODO This splice thing is very special and has to go again.  HACK!!
    * TODO a_go_input() will drop it once it sees EOF (HACK!), but care for
    * TODO jumps must be taken by splice creators.  HACK!!!  But works. ;} */
   a_GO_SPLICE = 1u<<7,
   /* If it is none of those, it must be the outermost, the global one */
   a_GO_TYPE_MASK = a_GO_PIPE | a_GO_FILE | a_GO_MACRO |
         /* a_GO_MACRO_X_OPTION | a_GO_MACRO_CMD | */ a_GO_SPLICE,

   a_GO_FORCE_EOF = 1u<<8,    /* go_input() shall return EOF next */

   a_GO_SUPER_MACRO = 1u<<16, /* *Not* inheriting n_PS_SOURCING state */
   /* This context has inherited the memory pool from its parent.
    * In practice only used for resource file loading and -X args, which enter
    * a top level n_go_main_loop() and should (re)use the in practice already
    * allocated memory pool of the global context */
   a_GO_MEMPOOL_INHERITED = 1u<<17,

   /* `xcall' optimization barrier: n_go_macro() has been finished with
    * a `xcall' request, and `xcall' set this in the parent a_go_input of the
    * said n_go_macro() to indicate a barrier: we teardown the a_go_input of
    * the n_go_macro() away after leaving its _event_loop(), but then,
    * back in n_go_macro(), that enters a for(;;) loop that directly calls
    * c_call() -- our `xcall' stack avoidance optimization --, yet this call
    * will itself end up in a new n_go_macro(), and if that again ends up with
    * `xcall' this should teardown and leave its own n_go_macro(), unrolling the
    * stack "up to the barrier level", but which effectively still is the
    * n_go_macro() that lost its a_go_input and is looping the `xcall'
    * optimization loop.  If no `xcall' is desired that loop is simply left and
    * the _event_loop() of the outer a_go_ctx will perform a loop tick and
    * clear this bit again OR become teardown itself */
   a_GO_XCALL_LOOP = 1u<<24   /* `xcall' optimization barrier level */
};

enum a_go_cleanup_mode{
   a_GO_CLEANUP_UNWIND = 1u<<0,     /* Teardown all contexts except outermost */
   a_GO_CLEANUP_TEARDOWN = 1u<<1,   /* Teardown current context */
   a_GO_CLEANUP_LOOPTICK = 1u<<2,   /* Normal looptick cleanup */
   a_GO_CLEANUP_MODE_MASK = n_BITENUM_MASK(0, 2),

   a_GO_CLEANUP_ERROR = 1u<<8,      /* Error occurred on level */
   a_GO_CLEANUP_SIGINT = 1u<<9,     /* Interrupt signal received */
   a_GO_CLEANUP_HOLDALLSIGS = 1u<<10 /* hold_all_sigs() active TODO */
};

struct a_go_cmd_desc{
   char const *gcd_name;   /* Name of command */
   int (*gcd_func)(void*); /* Implementor of command */
   enum n_cmd_arg_flags gcd_caflags;
   si16_t gcd_msgflag;     /* Required flags of msgs */
   si16_t gcd_msgmask;     /* Relevant flags of msgs */
#ifdef HAVE_DOCSTRINGS
   char const *gcd_doc;    /* One line doc for command */
#endif
};
/* Yechh, can't initialize unions */
#define gcd_minargs gcd_msgflag  /* Minimum argcount for WYSH/WYRA/RAWLIST */
#define gcd_maxargs gcd_msgmask  /* Max argcount for WYSH/WYRA/RAWLIST */

struct a_go_ghost{ /* TODO binary search */
   struct a_go_ghost *gg_next;
   struct str gg_cmd;            /* Data follows after .gg_name */
   char gg_name[n_VFIELD_SIZE(0)];
};

struct a_go_eval_ctx{
   struct str gec_line;    /* The terminated data to _evaluate() */
   ui32_t gec_line_size;   /* May be used to store line memory size */
   ui8_t gec__dummy[2];
   bool_t gec_ever_seen;   /* Has ever been used (main_loop() only) */
   bool_t gec_add_history; /* Add command to history (TRUM1=gabby)? */
};

struct a_go_input_inject{
   struct a_go_input_inject *gii_next;
   size_t gii_len;
   bool_t gii_commit;
   char gii_dat[n_VFIELD_SIZE(7)];
};

struct a_go_ctx{
   struct a_go_ctx *gc_outer;
   sigset_t gc_osigmask;
   ui32_t gc_flags;           /* enum a_go_flags */
   ui32_t gc_loff;            /* Pseudo (macro): index in .gc_lines */
   char **gc_lines;           /* Pseudo content, lines unfolded */
   FILE *gc_file;             /* File we were in, if applicable */
   struct a_go_input_inject *gc_inject; /* To be consumed first */
   void (*gc_on_finalize)(void *);
   void *gc_finalize_arg;
   sigjmp_buf gc_eloop_jmp;   /* TODO one day...  for _event_loop() */
   /* SPLICE hacks: saved stdin/stdout, saved pstate */
   FILE *gc_splice_stdin;
   FILE *gc_splice_stdout;
   ui32_t gc_splice_psonce;
   ui8_t gc_splice__dummy[4];
   struct n_go_data_ctx gc_data;
   char gc_name[n_VFIELD_SIZE(0)]; /* Name of file or macro */
};

struct a_go_xcall{
   struct a_go_ctx *gx_upto;  /* Unroll stack up to this level */
   size_t gx_buflen;          /* ARGV requires that much bytes, all in all */
   size_t gx_argc;
   struct str gx_argv[n_VFIELD_SIZE(0)];
};

static sighandler_type a_go_oldpipe;
static struct a_go_ghost *a_go_ghosts;
/* a_go_cmd_tab[] after fun protos */

/* Our current execution context, and the buffer backing the outermost level */
static struct a_go_ctx *a_go_ctx;

#define a_GO_MAINCTX_NAME "Main event loop"
static union{
   ui64_t align;
   char uf[n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name) +
         sizeof(a_GO_MAINCTX_NAME)];
   } a_go__mainctx_b;

/* `xcall' stack-avoidance bypass optimization */
static struct a_go_xcall *a_go_xcall;

static sigjmp_buf a_go_srbuf; /* TODO GET RID */

/* Isolate the command from the arguments */
static char *a_go_isolate(char const *comm);

/* `eval' */
static int a_go_c_eval(void *vp);

/* `xcall' */
static int a_go_c_xcall(void *vp);

/* Command ghost handling */
static int a_go_c_ghost(void *vp);
static int a_go_c_unghost(void *vp);

/* Create a multiline info string about all known additional infos for lcp */
#ifdef HAVE_DOCSTRINGS
static char const *a_go_cmdinfo(struct a_go_cmd_desc const *gcdp);
#endif

/* Print a list of all commands */
static int a_go_c_list(void *vp);

static int a_go__pcmd_cmp(void const *s1, void const *s2);

/* `help' / `?' command */
static int a_go_c_help(void *vp);

/* `exit' and `quit' commands */
static int a_go_c_exit(void *vp);
static int a_go_c_quit(void *vp);

/* Print the version number of the binary */
static int a_go_c_version(void *vp);

static int a_go__version_cmp(void const *s1, void const *s2);

/* n_PS_STATE_PENDMASK requires some actions */
static void a_go_update_pstate(void);

/* Evaluate a single command.
 * .gec_add_history will be updated upon success.
 * TODO Command functions return 0 for success, 1 for error, and -1 for abort.
 * 1 or -1 aborts a load or source, a -1 aborts the interactive command loop */
static int a_go_evaluate(struct a_go_eval_ctx *gecp);

/* Get first-fit, or NULL */
static struct a_go_cmd_desc const *a_go__firstfit(char const *comm);

/* Branch here on hangup signal and simulate "exit" */
static void a_go_hangup(int s);

/* The following gets called on receipt of an interrupt */
static void a_go_onintr(int s);

/* Cleanup current execution context, update the program state.
 * If _CLEANUP_ERROR is set then we don't alert and error out if the stack
 * doesn't exist at all, unless _CLEANUP_HOLDALLSIGS we hold_all_sigs() */
static void a_go_cleanup(enum a_go_cleanup_mode gcm);

/* `source' and `source_if' (if silent_open_error: no pipes allowed, then).
 * Returns FAL0 if file is somehow not usable (unless silent_open_error) or
 * upon evaluation error, and TRU1 on success */
static bool_t a_go_file(char const *file, bool_t silent_open_error);

/* System resource file load()ing or -X command line option array traversal */
static bool_t a_go_load(struct a_go_ctx *gcp);

/* A simplified command loop for recursed state machines */
static bool_t a_go_event_loop(struct a_go_ctx *gcp, enum n_go_input_flags gif);

/* `read' */
static int a_go_c_read(void *vp);

static bool_t a_go__read_set(char const *cp, char const *value);

/* List of all commands, and list of commands which are specially treated
 * and deduced in _evaluate(), but must offer normal descriptions for others */
#ifdef HAVE_DOCSTRINGS
# define DS(S) , S
#else
# define DS(S)
#endif
static struct a_go_cmd_desc const a_go_cmd_tab[] = {
#include "cmd-tab.h"
},
      a_go_special_cmd_tab[] = {
   { "#", NULL, n_CMD_ARG_TYPE_STRING, 0, 0
      DS(N_("Comment command: ignore remaining (continuable) line")) },
   { "-", NULL, n_CMD_ARG_TYPE_WYSH, 0, 0
      DS(N_("Print out the preceding message")) }
};
#undef DS

static char *
a_go_isolate(char const *comm){
   NYD2_ENTER;
   while(*comm != '\0' &&
         strchr("~|? \t0123456789&%@$^.:/-+*'\",;(`", *comm) == NULL)
      ++comm;
   NYD2_LEAVE;
   return n_UNCONST(comm);
}

static int
a_go_c_eval(void *vp){
   /* TODO HACK! `eval' should be nothing else but a command prefix, evaluate
    * TODO ARGV with shell rules, but if that is not possible then simply
    * TODO adjust argv/argc of "the CmdCtx" that we will have "exec" real cmd */
   struct n_string s_b, *sp;
   si32_t rv;
   size_t i, j;
   char const **argv, *cp;
   NYD_ENTER;

   argv = vp;

   for(j = i = 0; (cp = argv[i]) != NULL; ++i)
      j += strlen(cp);

   sp = n_string_creat_auto(&s_b);
   sp = n_string_reserve(sp, j);

   for(i = 0; (cp = argv[i]) != NULL; ++i){
      if(i > 0)
         sp = n_string_push_c(sp, ' ');
      sp = n_string_push_cp(sp, cp);
   }

   /* TODO HACK! We should inherit the current n_go_input_flags via CmdCtx,
    * TODO for now we don't have such sort of!  n_PS_COMPOSE_MODE is a hack
    * TODO by itself, since ever: misuse the hack for a hack.
    * TODO Further more, exit handling is very grazy */
   (void)/*XXX*/n_go_command((n_pstate & n_PS_COMPOSE_MODE
         ? n_GO_INPUT_CTX_COMPOSE : n_GO_INPUT_CTX_DEFAULT), n_string_cp(sp));

   if(a_go_xcall != NULL)
      rv = 0;
   else{
      cp = ok_vlook(__qm);
      if(cp == n_0) /* This is a hack, but since anything is a hack, be hacky */
         rv = 0;
      else if(cp == n_1)
         rv = 1;
      else if(cp == n_m1)
         rv = -1;
      else
         n_idec_si32_cp(&rv, cp, 10, NULL);
   }
   NYD_LEAVE;
   return rv;
}

static int
a_go_c_xcall(void *vp){
   struct a_go_xcall *gxp;
   size_t i, j;
   char *xcp;
   char const **oargv, *cp;
   int rv;
   struct a_go_ctx *gcp;
   NYD2_ENTER;

   /* The context can only be a macro context, except that possibly a single
    * level of `eval' (TODO: yet) was used to double-expand our arguments */
   if((gcp = a_go_ctx)->gc_flags & a_GO_MACRO_CMD)
      gcp = gcp->gc_outer;
   if((gcp->gc_flags & (a_GO_MACRO | a_GO_MACRO_CMD)) != a_GO_MACRO)
      goto jerr;

   /* Try to roll up the stack as much as possible.
    * See a_GO_XCALL_LOOP flag description for more */
   if(gcp->gc_outer != NULL){
      if(gcp->gc_outer->gc_flags & a_GO_XCALL_LOOP)
         gcp = gcp->gc_outer;
   }else{
      /* Otherwise this macro is invoked from the top level, in which case we
       * silently act as if we were `call'... */
      rv = c_call(vp);
      /* ...which means we must ensure the rest of the macro that was us
       * doesn't become evaluated! */
      a_go_xcall = (struct a_go_xcall*)-1;
      goto jleave;
   }

   oargv = vp;

   for(j = i = 0; (cp = oargv[i]) != NULL; ++i)
      j += strlen(cp) +1;

   gxp = n_alloc(n_VSTRUCT_SIZEOF(struct a_go_xcall, gx_argv) +
         (sizeof(struct str) * i) + ++j);
   gxp->gx_upto = gcp;
   gxp->gx_buflen = (sizeof(char*) * (i + 1)) + j; /* ARGV inc. strings! */
   gxp->gx_argc = i;
   xcp = (char*)&gxp->gx_argv[i];

   for(i = 0; (cp = oargv[i]) != NULL; ++i){
      gxp->gx_argv[i].l = j = strlen(cp);
      gxp->gx_argv[i].s = xcp;
      memcpy(xcp, cp, ++j);
      xcp += j;
   }

   a_go_xcall = gxp;
   rv = 0;
jleave:
   NYD2_LEAVE;
   return rv;
jerr:
   n_err(_("`xcall': can only be used inside a macro\n"));
   rv = 1;
   goto jleave;
}

static int
a_go_c_ghost(void *vp){
   struct a_go_ghost *lggp, *ggp;
   size_t i, cl, nl;
   char *cp;
   char const **argv;
   NYD_ENTER;

   argv = vp;

   /* Show the list? */
   if(*argv == NULL){
      FILE *fp;

      if((fp = Ftmp(NULL, "ghost", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
         fp = n_stdout;

      for(i = 0, ggp = a_go_ghosts; ggp != NULL; ggp = ggp->gg_next)
         fprintf(fp, "wysh ghost %s %s\n",
            ggp->gg_name, n_shexp_quote_cp(ggp->gg_cmd.s, TRU1));

      if(fp != n_stdout){
         page_or_print(fp, i);
         Fclose(fp);
      }
      goto jleave;
   }

   /* Verify the ghost name is a valid one, and not a command modifier */
   if(*argv[0] == '\0' || *a_go_isolate(argv[0]) != '\0' ||
         !asccasecmp(argv[0], "ignerr") || !asccasecmp(argv[0], "wysh") ||
         !asccasecmp(argv[0], "vput")){
      n_err(_("`ghost': can't canonicalize %s\n"),
         n_shexp_quote_cp(argv[0], FAL0));
      vp = NULL;
      goto jleave;
   }

   /* Show command of single ghost? */
   if(argv[1] == NULL){
      for(ggp = a_go_ghosts; ggp != NULL; ggp = ggp->gg_next)
         if(!strcmp(argv[0], ggp->gg_name)){
            fprintf(n_stdout, "wysh ghost %s %s\n",
               ggp->gg_name, n_shexp_quote_cp(ggp->gg_cmd.s, TRU1));
            goto jleave;
         }
      n_err(_("`ghost': no such alias: %s\n"), argv[0]);
      vp = NULL;
      goto jleave;
   }

   /* Define command for ghost: verify command content */
   for(cl = 0, i = 1; (cp = n_UNCONST(argv[i])) != NULL; ++i)
      if(*cp != '\0')
         cl += strlen(cp) +1; /* SP or NUL */
   if(cl == 0){
      n_err(_("`ghost': empty command arguments after %s\n"), argv[0]);
      vp = NULL;
      goto jleave;
   }

   /* If the ghost already exists, recreate */
   for(lggp = NULL, ggp = a_go_ghosts; ggp != NULL;
         lggp = ggp, ggp = ggp->gg_next)
      if(!strcmp(ggp->gg_name, argv[0])){
         if(lggp != NULL)
            lggp->gg_next = ggp->gg_next;
         else
            a_go_ghosts = ggp->gg_next;
         n_free(ggp);
         break;
      }

   nl = strlen(argv[0]) +1;
   ggp = n_alloc(n_VSTRUCT_SIZEOF(struct a_go_ghost, gg_name) + nl + cl);
   ggp->gg_next = a_go_ghosts;
   a_go_ghosts = ggp;
   memcpy(ggp->gg_name, argv[0], nl);
   cp = ggp->gg_cmd.s = &ggp->gg_name[nl];
   ggp->gg_cmd.l = --cl;

   while(*++argv != NULL)
      if((i = strlen(*argv)) > 0){
         memcpy(cp, *argv, i);
         cp += i;
         *cp++ = ' ';
      }
   *--cp = '\0';
jleave:
   NYD_LEAVE;
   return (vp == NULL);
}

static int
a_go_c_unghost(void *vp){
   struct a_go_ghost *lggp, *ggp;
   char const **argv, *cp;
   int rv;
   NYD_ENTER;

   rv = 0;
   argv = vp;

   while((cp = *argv++) != NULL){
      if(cp[0] == '*' && cp[1] == '\0'){
         while((ggp = a_go_ghosts) != NULL){
            a_go_ghosts = ggp->gg_next;
            n_free(ggp);
         }
      }else{
         for(lggp = NULL, ggp = a_go_ghosts; ggp != NULL;
               lggp = ggp, ggp = ggp->gg_next)
            if(!strcmp(ggp->gg_name, cp)){
               if(lggp != NULL)
                  lggp->gg_next = ggp->gg_next;
               else
                  a_go_ghosts = ggp->gg_next;
               n_free(ggp);
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

#ifdef HAVE_DOCSTRINGS
static char const *
a_go_cmdinfo(struct a_go_cmd_desc const *gcdp){
   struct n_string rvb, *rv;
   char const *cp;
   NYD2_ENTER;

   rv = n_string_creat_auto(&rvb);
   rv = n_string_reserve(rv, 80);

   switch(gcdp->gcd_caflags & n_CMD_ARG_TYPE_MASK){
   case n_CMD_ARG_TYPE_MSGLIST:
      cp = N_("message-list");
      break;
   case n_CMD_ARG_TYPE_STRING:
   case n_CMD_ARG_TYPE_RAWDAT:
      cp = N_("string data");
      break;
   case n_CMD_ARG_TYPE_RAWLIST:
      cp = N_("old-style quoting");
      break;
   case n_CMD_ARG_TYPE_NDMLIST:
      cp = N_("message-list (no default)");
      break;
   case n_CMD_ARG_TYPE_WYRA:
      cp = N_("`wysh' for sh(1)ell-style quoting");
      break;
   default:
   case n_CMD_ARG_TYPE_WYSH:
      cp = (gcdp->gcd_minargs == 0 && gcdp->gcd_maxargs == 0)
            ? N_("sh(1)ell-style quoting (takes no arguments)")
            : N_("sh(1)ell-style quoting");
      break;
   }
   rv = n_string_push_cp(rv, V_(cp));

   if(gcdp->gcd_caflags & n_CMD_ARG_V)
      rv = n_string_push_cp(rv, _(" | vput modifier"));
   if(gcdp->gcd_caflags & n_CMD_ARG_EM)
      rv = n_string_push_cp(rv, _(" | status in *!*"));

   if(gcdp->gcd_caflags & n_CMD_ARG_A)
      rv = n_string_push_cp(rv, _(" | needs box"));
   if(gcdp->gcd_caflags & n_CMD_ARG_I)
      rv = n_string_push_cp(rv, _(" | ok: batch or interactive"));
   if(gcdp->gcd_caflags & n_CMD_ARG_M)
      rv = n_string_push_cp(rv, _(" | ok: send mode"));
   if(gcdp->gcd_caflags & n_CMD_ARG_R)
      rv = n_string_push_cp(rv, _(" | not ok: compose mode"));
   if(gcdp->gcd_caflags & n_CMD_ARG_S)
      rv = n_string_push_cp(rv, _(" | not ok: during startup"));
   if(gcdp->gcd_caflags & n_CMD_ARG_X)
      rv = n_string_push_cp(rv, _(" | ok: in subprocess"));

   if(gcdp->gcd_caflags & n_CMD_ARG_G)
      rv = n_string_push_cp(rv, _(" | gabby history"));

   cp = n_string_cp(rv);
   NYD2_LEAVE;
   return cp;
}
#endif /* HAVE_DOCSTRINGS */

static int
a_go_c_list(void *vp){
   FILE *fp;
   struct a_go_cmd_desc const **gcdpa, *gcdp, **gcdpa_curr;
   size_t i, scrwid, l;
   NYD_ENTER;

   i = n_NELEM(a_go_cmd_tab) + n_NELEM(a_go_special_cmd_tab) +1;
   gcdpa = n_autorec_alloc(sizeof(gcdp) * i);

   for(i = 0; i < n_NELEM(a_go_cmd_tab); ++i)
      gcdpa[i] = &a_go_cmd_tab[i];
   /* C99 */{
      size_t j;

      for(j = 0; j < n_NELEM(a_go_special_cmd_tab); ++i, ++j)
         gcdpa[i] = &a_go_special_cmd_tab[j];
   }
   gcdpa[i] = NULL;

   if(*(void**)vp == NULL)
      qsort(gcdpa, i, sizeof(*gcdpa), &a_go__pcmd_cmp);

   if((fp = Ftmp(NULL, "list", OF_RDWR | OF_UNLINK | OF_REGISTER)) == NULL)
      fp = n_stdout;

   fprintf(fp, _("Commands are:\n"));
   scrwid = n_SCRNWIDTH_FOR_LISTS;
   l = 1;
   for(i = 0, gcdpa_curr = gcdpa; (gcdp = *gcdpa_curr++) != NULL;){
      if(gcdp->gcd_func == &c_cmdnotsupp)
         continue;
      if(n_poption & n_PO_D_V){
         fprintf(fp, "%s\n", gcdp->gcd_name);
         ++l;
#ifdef HAVE_DOCSTRINGS
         fprintf(fp, "  : %s\n", V_(gcdp->gcd_doc));
         ++l;
         fprintf(fp, "  : %s\n", a_go_cmdinfo(gcdp));
         ++l;
#endif
      }else{
         size_t j;

         if((i += (j = strlen(gcdp->gcd_name) + 2)) > scrwid){
            i = j;
            fprintf(fp, "\n");
            ++l;
         }
         fprintf(fp, (*gcdpa_curr != NULL ? "%s, " : "%s\n"), gcdp->gcd_name);
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
a_go__pcmd_cmp(void const *s1, void const *s2){
   struct a_go_cmd_desc const * const *gcdpa1, * const *gcdpa2;
   int rv;
   NYD2_ENTER;

   gcdpa1 = s1;
   gcdpa2 = s2;
   rv = strcmp((*gcdpa1)->gcd_name, (*gcdpa2)->gcd_name);
   NYD2_LEAVE;
   return rv;
}

static int
a_go_c_help(void *vp){
   int rv;
   char *arg;
   NYD_ENTER;

   /* Help for a single command? */
   if((arg = *(char**)vp) != NULL){
      struct a_go_ghost const *ggp;
      struct a_go_cmd_desc const *gcdp, *gcdp_max;

      /* Ghosts take precedence */
      for(ggp = a_go_ghosts; ggp != NULL; ggp = ggp->gg_next)
         if(!strcmp(arg, ggp->gg_name)){
            fprintf(n_stdout, "%s -> ", arg);
            arg = ggp->gg_cmd.s;
            break;
         }

      gcdp_max = &(gcdp = a_go_cmd_tab)[n_NELEM(a_go_cmd_tab)];
jredo:
      for(; gcdp < gcdp_max; ++gcdp){
         if(is_prefix(arg, gcdp->gcd_name)){
            fputs(arg, n_stdout);
            if(strcmp(arg, gcdp->gcd_name))
               fprintf(n_stdout, " (%s)", gcdp->gcd_name);
         }else
            continue;

#ifdef HAVE_DOCSTRINGS
         fprintf(n_stdout, ": %s", V_(gcdp->gcd_doc));
         if(n_poption & n_PO_D_V)
            fprintf(n_stdout, "\n  : %s", a_go_cmdinfo(gcdp));
#endif
         putc('\n', n_stdout);
         rv = 0;
         goto jleave;
      }

      if(gcdp_max == &a_go_cmd_tab[n_NELEM(a_go_cmd_tab)]){
         gcdp_max = &(gcdp =
               a_go_special_cmd_tab)[n_NELEM(a_go_special_cmd_tab)];
         goto jredo;
      }

      if(ggp != NULL){
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
"type <msglist>         type (`print') messages (honour `headerpick' etc.)\n"
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
a_go_c_exit(void *vp){
   NYD_ENTER;
   n_UNUSED(vp);

   if(n_psonce & n_PSO_STARTED){
      /* In recursed state, return error to just pop the input level */
      if(!(n_pstate & n_PS_SOURCING)){ /* FIXME a_go_ctx->gc_outer == NULL */
#ifdef n_HAVE_TCAP
         if((n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_QUICKRUN_MASK))
            n_termcap_destroy();
#endif
         exit(n_EXIT_OK);
      }
   }
   n_pstate |= n_PS_EXIT; /* FIXME Always like this here, then CLEANUP_UNWIND!*/
   NYD_LEAVE;
   return 0;
}

static int
a_go_c_quit(void *vp){
   NYD_ENTER;
   n_UNUSED(vp);
   n_pstate |= n_PS_EXIT;
   NYD_LEAVE;
   return 0;
}

static int
a_go_c_version(void *vp){
   int longest, rv;
   char *iop;
   char const *cp, **arr;
   size_t i, i2;
   NYD_ENTER;
   n_UNUSED(vp);

   fprintf(n_stdout, _("%s version %s\nFeatures included (+) or not (-)\n"),
      n_uagent, ok_vlook(version));

   /* *features* starts with dummy byte to avoid + -> *folder* expansions */
   i = strlen(cp = &ok_vlook(features)[1]) +1;
   iop = n_autorec_alloc(i);
   memcpy(iop, cp, i);

   arr = n_autorec_alloc(sizeof(cp) * VAL_FEATURES_CNT);
   for(longest = 0, i = 0; (cp = n_strsep(&iop, ',', TRU1)) != NULL; ++i){
      arr[i] = cp;
      i2 = strlen(cp);
      longest = n_MAX(longest, (int)i2);
   }
   qsort(arr, i, sizeof(cp), &a_go__version_cmp);

   /* We use aligned columns, so don't use n_SCRNWIDTH_FOR_LISTS */
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
a_go__version_cmp(void const *s1, void const *s2){
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
a_go_update_pstate(void){
   bool_t act;
   NYD_ENTER;

   act = ((n_pstate & n_PS_SIGWINCH_PEND) != 0);
   n_pstate &= ~n_PS_PSTATE_PENDMASK;

   if(act){
      char buf[32];

      snprintf(buf, sizeof buf, "%d", n_scrnwidth);
      ok_vset(COLUMNS, buf);
      snprintf(buf, sizeof buf, "%d", n_scrnheight);
      ok_vset(LINES, buf);
   }
   NYD_LEAVE;
}

static int
a_go_evaluate(struct a_go_eval_ctx *gecp){
   /* xxx old style(9), but also old code */
   struct str line;
   char _wordbuf[2], **arglist_base/*[n_MAXARGC]*/, **arglist, *cp, *word;
   struct a_go_ghost *ggp;
   struct a_go_cmd_desc const *gcdp;
   int rv, c;
   enum {
      a_NONE = 0,
      a_GHOST_MASK = n_BITENUM_MASK(0, 2), /* Alias recursion counter bits */
      a_NOPREFIX = 1<<4,   /* Modifier prefix not allowed right now */
      a_NOGHOST = 1<<5,    /* "No alias!" expansion modifier */
      /* New modifier prefixes must be reflected in a_go_c_alias()! */
      a_IGNERR = 1<<6,     /* ignerr modifier prefix */
      a_WYSH = 1<<7,       /* XXX v15+ drop wysh modifier prefix */
      a_VPUT = 1<<8        /* vput modifier prefix */
   } flags;
   NYD_ENTER;

   flags = a_NONE;
   rv = 1;
   gcdp = NULL;
   ggp = NULL;
   arglist =
   arglist_base = n_autorec_alloc(sizeof(*arglist_base) * n_MAXARGC);
   line = gecp->gec_line; /* XXX don't change original (buffer pointer) */
   assert(line.s[line.l] == '\0');
   gecp->gec_add_history = FAL0;

   /* Command ghosts that refer to shell commands or macro expansion restart */
jrestart:

   /* Strip the white space away from end and beginning of command */
   if(line.l > 0){
      size_t i;

      i = line.l;
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
   else if((cp = a_go_isolate(cp)) == arglist[0] &&
         (*cp == '|' || *cp == '~' || *cp == '?'))
      ++cp;
   c = (int)PTR2SIZE(cp - arglist[0]);
   line.l -= c;
   word = UICMP(z, c, <, sizeof _wordbuf) ? _wordbuf : n_autorec_alloc(c +1);
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
      if((n_pstate & n_PS_ROBOT) || !(n_psonce & n_PSO_INTERACTIVE) ||
            ggp != NULL)
         goto jerr0;
      gcdp = &a_go_cmd_tab[0];
      goto jexec;
   }

   if(!(flags & a_NOGHOST) && (flags & a_GHOST_MASK) != a_GHOST_MASK){
      ui8_t expcnt;

      expcnt = (flags & a_GHOST_MASK);
      ++expcnt;
      flags = (flags & ~(a_GHOST_MASK | a_NOPREFIX)) | expcnt;

      /* Avoid self-recursion; yes, the user could use \ no-expansion, but.. */
      if(ggp != NULL && !strcmp(word, ggp->gg_name)){
         if(n_poption & n_PO_D_V)
            n_err(_("Actively avoiding self-recursion of `ghost': %s\n"), word);
      }else for(ggp = a_go_ghosts; ggp != NULL; ggp = ggp->gg_next)
         if(!strcmp(word, ggp->gg_name)){
            if(line.l > 0){
               size_t i;

               i = ggp->gg_cmd.l;
               line.s = n_autorec_alloc(i + line.l +1);
               memcpy(line.s, ggp->gg_cmd.s, i);
               memcpy(line.s + i, cp, line.l);
               line.s[i += line.l] = '\0';
               line.l = i;
            }else{
               line.s = ggp->gg_cmd.s;
               line.l = ggp->gg_cmd.l;
            }
            goto jrestart;
         }
   }

   if((gcdp = a_go__firstfit(word)) == NULL || gcdp->gcd_func == &c_cmdnotsupp){
      bool_t s;

      if(!(s = n_cnd_if_isskip()) || (n_poption & n_PO_D_V))
         n_err(_("Unknown command%s: `%s'\n"),
            (s ? _(" (ignored due to `if' condition)") : n_empty), word);
      if(s)
         goto jerr0;
      if(gcdp != NULL){
         c_cmdnotsupp(NULL);
         gcdp = NULL;
      }
      n_pstate_var__em = n_m1;
      goto jleave;
   }

   /* See if we should execute the command -- if a conditional we always
    * execute it, otherwise, check the state of cond */
jexec:
   if(!(gcdp->gcd_caflags & n_CMD_ARG_F) && n_cnd_if_isskip())
      goto jerr0;

   n_pstate_var__em = n_1;

   /* Process the arguments to the command, depending on the type it expects */
   if((gcdp->gcd_caflags & n_CMD_ARG_I) && !(n_psonce & n_PSO_INTERACTIVE) &&
         !(n_poption & n_PO_BATCH_FLAG)){
      n_err(_("May not execute `%s' unless interactive or in batch mode\n"),
         gcdp->gcd_name);
      goto jleave;
   }
   if(!(gcdp->gcd_caflags & n_CMD_ARG_M) && (n_psonce & n_PSO_SENDMODE)){
      n_err(_("May not execute `%s' while sending\n"), gcdp->gcd_name);
      goto jleave;
   }
   if(gcdp->gcd_caflags & n_CMD_ARG_R){
      if(n_pstate & n_PS_COMPOSE_MODE){
         /* TODO n_PS_COMPOSE_MODE: should allow `reply': ~:reply! */
         n_err(_("Cannot invoke `%s' when in compose mode\n"), gcdp->gcd_name);
         goto jleave;
      }
      /* TODO Nothing should prevent n_CMD_ARG_R in conjunction with
       * TODO n_PS_ROBOT|_SOURCING; see a.._may_yield_control()! */
      if(n_pstate & (n_PS_ROBOT | n_PS_SOURCING)){
         n_err(_("Cannot invoke `%s' from a macro or during file inclusion\n"),
            gcdp->gcd_name);
         goto jleave;
      }
   }
   if((gcdp->gcd_caflags & n_CMD_ARG_S) && !(n_psonce & n_PSO_STARTED)){
      n_err(_("May not execute `%s' during startup\n"), gcdp->gcd_name);
      goto jleave;
   }
   if(!(gcdp->gcd_caflags & n_CMD_ARG_X) && (n_pstate & n_PS_COMPOSE_FORKHOOK)){
      n_err(_("Cannot invoke `%s' from a hook running in a child process\n"),
         gcdp->gcd_name);
      goto jleave;
   }

   if((gcdp->gcd_caflags & n_CMD_ARG_A) && mb.mb_type == MB_VOID){
      n_err(_("Cannot execute `%s' without active mailbox\n"), gcdp->gcd_name);
      goto jleave;
   }
   if((gcdp->gcd_caflags & n_CMD_ARG_W) && !(mb.mb_perm & MB_DELE)){
      n_err(_("May not execute `%s' -- message file is read only\n"),
         gcdp->gcd_name);
      goto jleave;
   }

   if(gcdp->gcd_caflags & n_CMD_ARG_O)
      n_OBSOLETE2(_("this command will be removed"), gcdp->gcd_name);

   /* TODO v15: strip n_PS_ARGLIST_MASK off, just in case the actual command
    * TODO doesn't use any of those list commands which strip this mask,
    * TODO and for now we misuse bits for checking relation to history;
    * TODO argument state should be property of a per-command carrier instead */
   n_pstate &= ~n_PS_ARGLIST_MASK;

   if((flags & a_WYSH) &&
         (gcdp->gcd_caflags & n_CMD_ARG_TYPE_MASK) != n_CMD_ARG_TYPE_WYRA){
      n_err(_("`wysh' prefix does not affect `%s'\n"), gcdp->gcd_name);
      flags &= ~a_WYSH;
   }

   if(flags & a_VPUT){
      if(gcdp->gcd_caflags & n_CMD_ARG_V){
         char const *xcp;

         xcp = cp;
         arglist[0] = n_shexp_parse_token_cp((n_SHEXP_PARSE_TRIMSPACE |
               n_SHEXP_PARSE_LOG), &xcp);
         line.l -= PTR2SIZE(xcp - cp);
         cp = n_UNCONST(xcp);
         if(cp == NULL)
            xcp = N_("could not parse input token");
         else if(!n_shexp_is_valid_varname(arglist[0]))
            xcp = N_("not a valid variable name");
         else if(!n_var_is_user_writable(arglist[0]))
            xcp = N_("either not a user writable, or a boolean variable");
         else
            xcp = NULL;
         if(xcp != NULL){
            n_err("`%s': vput: %s: %s\n",
                  gcdp->gcd_name, V_(xcp), n_shexp_quote_cp(arglist[0], FAL0));
            goto jleave;
         }
         ++arglist;
         n_pstate |= n_PS_ARGMOD_VPUT; /* TODO YET useless since stripped later
         * TODO on in getrawlist() etc., i.e., the argument vector producers,
         * TODO therefore yet needs to be set again based on flags&a_VPUT! */
      }else{
         n_err(_("`vput' prefix does not affect `%s'\n"), gcdp->gcd_name);
         flags &= ~a_VPUT;
      }
   }

   switch(gcdp->gcd_caflags & n_CMD_ARG_TYPE_MASK){
   case n_CMD_ARG_TYPE_MSGLIST:
      /* Message list defaulting to nearest forward legal message */
      if(n_msgvec == NULL)
         goto je96;
      if((c = getmsglist(cp, n_msgvec, gcdp->gcd_msgflag)) < 0)
         break;
      if(c == 0){
         if((n_msgvec[0] = first(gcdp->gcd_msgflag, gcdp->gcd_msgmask)) != 0)
            n_msgvec[1] = 0;
      }
      if(n_msgvec[0] == 0){
         if(!(n_pstate & n_PS_HOOK_MASK))
            fprintf(n_stdout, _("No applicable messages\n"));
         break;
      }
      rv = (*gcdp->gcd_func)(n_msgvec);
      break;

   case n_CMD_ARG_TYPE_NDMLIST:
      /* Message list with no defaults, but no error if none exist */
      if(n_msgvec == NULL){
je96:
         n_err(_("Invalid use of message list\n"));
         break;
      }
      if((c = getmsglist(cp, n_msgvec, gcdp->gcd_msgflag)) < 0)
         break;
      rv = (*gcdp->gcd_func)(n_msgvec);
      break;

   case n_CMD_ARG_TYPE_STRING:
      /* Just the straight string, old style, with leading blanks removed */
      while(spacechar(*cp))
         ++cp;
      rv = (*gcdp->gcd_func)(cp);
      break;
   case n_CMD_ARG_TYPE_RAWDAT:
      /* Just the straight string, leading blanks removed, placed in argv[] */
      while(spacechar(*cp))
         ++cp;
      *arglist++ = cp;
      *arglist = NULL;
      rv = (*gcdp->gcd_func)(arglist_base);
      break;

   case n_CMD_ARG_TYPE_WYSH:
      c = 1;
      if(0){
         /* FALLTHRU */
   case n_CMD_ARG_TYPE_WYRA:
         c = (flags & a_WYSH) ? 1 : 0;
         if(0){
   case n_CMD_ARG_TYPE_RAWLIST:
            c = 0;
         }
      }
      if((c = getrawlist((c != 0), arglist,
            n_MAXARGC - PTR2SIZE(arglist - arglist_base),
            cp, line.l)) < 0){
         n_err(_("Invalid argument list\n"));
         break;
      }

      if(c < gcdp->gcd_minargs){
         n_err(_("`%s' requires at least %u arg(s)\n"),
            gcdp->gcd_name, (ui32_t)gcdp->gcd_minargs);
         break;
      }
#undef gcd_minargs
      if(c > gcdp->gcd_maxargs){
         n_err(_("`%s' takes no more than %u arg(s)\n"),
            gcdp->gcd_name, (ui32_t)gcdp->gcd_maxargs);
         break;
      }
#undef gcd_maxargs

      if(flags & a_VPUT)
         n_pstate |= n_PS_ARGMOD_VPUT;

      rv = (*gcdp->gcd_func)(arglist_base);
      if(a_go_xcall != NULL)
         goto jret0;
      break;

   default:
      DBG( n_panic(_("Implementation error: unknown argument type: %d"),
         gcdp->gcd_caflags & n_CMD_ARG_TYPE_MASK); )
      goto jerr0;
   }

   if(!(gcdp->gcd_caflags & n_CMD_ARG_H))
      gecp->gec_add_history = (((gcdp->gcd_caflags & n_CMD_ARG_G) ||
            (n_pstate & n_PS_MSGLIST_GABBY)) ? TRUM1 : TRU1);

   if(!(gcdp->gcd_caflags & n_CMD_ARG_EM) && rv == 0)
      n_pstate_var__em = n_0;
jleave:
   n_PS_ROOT_BLOCK(
      ok_vset(__qm, (rv == 0 ? n_0 : n_1));
      ok_vset(__em, n_pstate_var__em)
   );

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

   if(gcdp == NULL)
      goto jret0;
   if((gcdp->gcd_caflags & n_CMD_ARG_P) && ok_blook(autoprint))
      if(visible(dot))
         n_go_input_inject(n_GO_INPUT_INJECT_COMMIT, "\\type",
            sizeof("\\type") -1);

   if(!(n_pstate & (n_PS_SOURCING | n_PS_HOOK_MASK)) &&
         !(gcdp->gcd_caflags & n_CMD_ARG_T))
      n_pstate |= n_PS_SAW_COMMAND;
jleave0:
   n_pstate &= ~n_PS_EVAL_ERROR;
jret0:
   rv = 0;
jret:
   NYD_LEAVE;
   return rv;
jerr0:
   n_PS_ROOT_BLOCK(
      ok_vset(__qm, n_0);
      ok_vset(__em, n_0)
   );
   goto jleave0;
}

static struct a_go_cmd_desc const *
a_go__firstfit(char const *comm){ /* TODO *hashtable*! linear list search!!! */
   struct a_go_cmd_desc const *gcdp;
   NYD2_ENTER;

   for(gcdp = a_go_cmd_tab; gcdp < &a_go_cmd_tab[n_NELEM(a_go_cmd_tab)]; ++gcdp)
      if(*comm == *gcdp->gcd_name && is_prefix(comm, gcdp->gcd_name))
         goto jleave;
   gcdp = NULL;
jleave:
   NYD2_LEAVE;
   return gcdp;
}

static void
a_go_hangup(int s){
   NYD_X; /* Signal handler */
   n_UNUSED(s);
   /* nothing to do? */
   exit(n_EXIT_ERR);
}

static void
a_go_onintr(int s){ /* TODO block signals while acting */
   NYD_X; /* Signal handler */
   n_UNUSED(s);

   safe_signal(SIGINT, a_go_onintr);

   termios_state_reset();

   a_go_cleanup(a_GO_CLEANUP_UNWIND | /* XXX FAKE */a_GO_CLEANUP_HOLDALLSIGS);

   if(interrupts != 1)
      n_err_sighdl(_("Interrupt\n"));
   safe_signal(SIGPIPE, a_go_oldpipe);
   siglongjmp(a_go_srbuf, 0); /* FIXME get rid */
}

static void
a_go_cleanup(enum a_go_cleanup_mode gcm){
   /* Signals blocked */
   struct a_go_ctx *gcp;
   NYD_ENTER;

   if(!(gcm & a_GO_CLEANUP_HOLDALLSIGS))
      hold_all_sigs();
jrestart:
   gcp = a_go_ctx;

   /* Free input injections of this level first */
   if(!(gcm & a_GO_CLEANUP_LOOPTICK)){
      struct a_go_input_inject **giipp, *giip;

      for(giipp = &gcp->gc_inject; (giip = *giipp) != NULL;){
         *giipp = giip->gii_next;
         n_free(giip);
      }
   }

   /* Cleanup non-crucial external stuff */
   n_COLOUR(
      if(gcp->gc_data.gdc_colour != NULL)
         n_colour_stack_del(NULL);
   )

   /* Work the actual context (according to cleanup mode) */
   if(gcp->gc_outer == NULL){
      if(gcm & (a_GO_CLEANUP_UNWIND | a_GO_CLEANUP_SIGINT)){
         a_go_xcall = NULL;
         gcp->gc_flags &= ~a_GO_XCALL_LOOP;
         close_all_files();
      }else if(!(n_pstate & n_PS_SOURCING))
         close_all_files();

      n_memory_reset();

      n_pstate &= ~(n_PS_SOURCING | n_PS_ROBOT);
      assert(a_go_xcall == NULL);
      assert(!(gcp->gc_flags & a_GO_XCALL_LOOP));
      assert(gcp->gc_on_finalize == NULL);
      assert(gcp->gc_data.gdc_colour == NULL);
      goto jxleave;
   }else if(gcm & a_GO_CLEANUP_LOOPTICK){
      n_memory_reset();
      goto jxleave;
   }else if(gcp->gc_flags & a_GO_SPLICE){ /* TODO Temporary hack */
      n_stdin = gcp->gc_splice_stdin;
      n_stdout = gcp->gc_splice_stdout;
      n_psonce = gcp->gc_splice_psonce;
      goto jstackpop;
   }

   /* Cleanup crucial external stuff */
   if(gcp->gc_data.gdc_ifcond != NULL){
      n_cnd_if_stack_del(gcp->gc_data.gdc_ifcond);
      if(!(gcm & (a_GO_CLEANUP_ERROR | a_GO_CLEANUP_SIGINT)) &&
            a_go_xcall == NULL)
         n_err(_("Unmatched `if' at end of %s %s\n"),
            ((gcp->gc_flags & a_GO_MACRO
             ? (gcp->gc_flags & a_GO_MACRO_CMD ? _("command") : _("macro"))
             : _("`source'd file"))),
            gcp->gc_name);
      gcm |= a_GO_CLEANUP_ERROR;
   }

   /* Teardown context */
   if(gcp->gc_flags & a_GO_MACRO){
      if(gcp->gc_flags & a_GO_MACRO_FREE_DATA){
         char **lp;

         while(*(lp = &gcp->gc_lines[gcp->gc_loff]) != NULL){
            n_free(*lp);
            ++gcp->gc_loff;
         }
         /* Part of gcp's memory chunk, then */
         if(!(gcp->gc_flags & a_GO_MACRO_CMD))
            n_free(gcp->gc_lines);
      }
   }else if(gcp->gc_flags & a_GO_PIPE)
      /* XXX command manager should -TERM then -KILL instead of hoping
       * XXX for exit of provider due to n_ERR_PIPE / SIGPIPE */
      Pclose(gcp->gc_file, TRU1);
   else if(gcp->gc_flags & a_GO_FILE)
      Fclose(gcp->gc_file);

   if(!(gcp->gc_flags & a_GO_MEMPOOL_INHERITED)){
      if(gcp->gc_data.gdc_mempool != NULL)
         n_memory_pool_pop(NULL);
   }else
      n_memory_reset();

jstackpop:
   n_go_data = &(a_go_ctx = gcp->gc_outer)->gc_data;
   if((a_go_ctx->gc_flags & (a_GO_MACRO | a_GO_SUPER_MACRO)) ==
         (a_GO_MACRO | a_GO_SUPER_MACRO)){
      n_pstate &= ~n_PS_SOURCING;
      assert(n_pstate & n_PS_ROBOT);
   }else if(!(a_go_ctx->gc_flags & a_GO_TYPE_MASK))
      n_pstate &= ~(n_PS_SOURCING | n_PS_ROBOT);
   else
      assert(n_pstate & n_PS_ROBOT);

   if(gcp->gc_on_finalize != NULL)
      (*gcp->gc_on_finalize)(gcp->gc_finalize_arg);

   if(gcm & a_GO_CLEANUP_ERROR)
      goto jerr;
jleave:
   if(gcp->gc_flags & a_GO_FREE)
      n_free(gcp);

   if(n_UNLIKELY((gcm & a_GO_CLEANUP_UNWIND) && gcp != a_go_ctx))
      goto jrestart;

jxleave:
   NYD_LEAVE;
   if(!(gcm & a_GO_CLEANUP_HOLDALLSIGS))
      rele_all_sigs();
   return;

jerr:
   /* With *posix* we follow what POSIX says:
    *    Any errors in the start-up file shall either cause mailx to
    *    terminate with a diagnostic message and a non-zero status or to
    *    continue after writing a diagnostic message, ignoring the
    *    remainder of the lines in the start-up file
    * Print the diagnostic only for the outermost resource unless the user
    * is debugging or in verbose mode */
   if((n_poption & n_PO_D_V) ||
         (!(n_psonce & n_PSO_STARTED) &&
          !(gcp->gc_flags & (a_GO_SPLICE | a_GO_MACRO)) &&
          !(gcp->gc_outer->gc_flags & a_GO_TYPE_MASK)))
      /* I18N: file inclusion, macro etc. evaluation has been stopped */
      n_alert(_("Stopped %s %s due to errors%s"),
         (n_psonce & n_PSO_STARTED
          ? (gcp->gc_flags & a_GO_SPLICE ? _("spliced in program")
          : (gcp->gc_flags & a_GO_MACRO
             ? (gcp->gc_flags & a_GO_MACRO_CMD
                ? _("evaluating command") : _("evaluating macro"))
             : (gcp->gc_flags & a_GO_PIPE
                ? _("executing `source'd pipe")
                : (gcp->gc_flags & a_GO_FILE
                  ? _("loading `source'd file") : _(a_GO_MAINCTX_NAME))))
          )
          : (gcp->gc_flags & a_GO_MACRO
             ? (gcp->gc_flags & a_GO_MACRO_X_OPTION
                ? _("evaluating command line") : _("evaluating macro"))
             : _("loading initialization resource"))),
         gcp->gc_name,
         (n_poption & n_PO_DEBUG ? n_empty : _(" (enable *debug* for trace)")));

   if(!(n_psonce & (n_PSO_INTERACTIVE | n_PSO_STARTED)) && ok_blook(posix)){
      if(n_poption & n_PO_D_V)
         n_alert(_("Non-interactive, bailing out due to errors "
            "in startup load phase"));
      exit(n_EXIT_ERR);
   }
   goto jleave;
}

static bool_t
a_go_file(char const *file, bool_t silent_open_error){
   struct a_go_ctx *gcp;
   sigset_t osigmask;
   size_t nlen;
   char *nbuf;
   bool_t ispipe;
   FILE *fip;
   NYD_ENTER;

   fip = NULL;

   /* Being a command argument file is space-trimmed *//* TODO v15 with
    * TODO WYRALIST this is no longer necessary true, and for that we
    * TODO don't set _PARSE_TRIMSPACE because we cannot! -> cmd-tab.h!! */
#if 0
   ((ispipe = (!silent_open_error && (nlen = strlen(file)) > 0 &&
         file[--nlen] == '|')))
#else
   ispipe = FAL0;
   if(!silent_open_error){
      for(nlen = strlen(file); nlen > 0;){
         char c;

         c = file[--nlen];
         if(!spacechar(c)){
            if(c == '|'){
               nbuf = savestrbuf(file, nlen);
               ispipe = TRU1;
            }
            break;
         }
      }
   }
#endif

   if(ispipe){
      if((fip = Popen(nbuf /* #if 0 above = savestrbuf(file, nlen)*/, "r",
            ok_vlook(SHELL), NULL, n_CHILD_FD_NULL)) == NULL)
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

   sigprocmask(SIG_BLOCK, NULL, &osigmask);

   gcp = n_alloc(n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name) +
         (nlen = strlen(nbuf) +1));
   memset(gcp, 0, n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name));

   hold_all_sigs();

   gcp->gc_outer = a_go_ctx;
   gcp->gc_osigmask = osigmask;
   gcp->gc_file = fip;
   gcp->gc_flags = (ispipe ? a_GO_FREE | a_GO_PIPE : a_GO_FREE | a_GO_FILE) |
         (a_go_ctx->gc_flags & a_GO_SUPER_MACRO ? a_GO_SUPER_MACRO : 0);
   memcpy(gcp->gc_name, nbuf, nlen);

   a_go_ctx = gcp;
   n_go_data = &gcp->gc_data;
   n_pstate |= n_PS_SOURCING | n_PS_ROBOT;
   if(!a_go_event_loop(gcp, n_GO_INPUT_NONE | n_GO_INPUT_NL_ESC))
      fip = NULL;
jleave:
   NYD_LEAVE;
   return (fip != NULL);
}

static bool_t
a_go_load(struct a_go_ctx *gcp){
   bool_t rv;
   NYD2_ENTER;

   assert(!(n_psonce & n_PSO_STARTED));
   assert(!(a_go_ctx->gc_flags & a_GO_TYPE_MASK));

   gcp->gc_flags |= a_GO_MEMPOOL_INHERITED;
   gcp->gc_data.gdc_mempool = n_go_data->gdc_mempool;

   hold_all_sigs();

   /* POSIX:
    *    Any errors in the start-up file shall either cause mailx to terminate
    *    with a diagnostic message and a non-zero status or to continue after
    *    writing a diagnostic message, ignoring the remainder of the lines in
    *    the start-up file. */
   gcp->gc_outer = a_go_ctx;
   a_go_ctx = gcp;
   n_go_data = &gcp->gc_data;
/* FIXME won't work for now (n_PS_ROBOT needs n_PS_SOURCING sofar)
   n_pstate |= n_PS_ROBOT |
         (gcp->gc_flags & a_GO_MACRO_X_OPTION ? 0 : n_PS_SOURCING);
*/
   n_pstate |= n_PS_ROBOT | n_PS_SOURCING;

   rele_all_sigs();

   if(!(rv = n_go_main_loop())){
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
a_go__eloopint(int sig){ /* TODO one day, we don't need it no more */
   NYD_X; /* Signal handler */
   n_UNUSED(sig);
   siglongjmp(a_go_ctx->gc_eloop_jmp, 1);
}

static bool_t
a_go_event_loop(struct a_go_ctx *gcp, enum n_go_input_flags gif){
   volatile int hadint; /* TODO get rid of shitty signal stuff (see signal.c) */
   sighandler_type soldhdl;
   struct a_go_eval_ctx gec;
   bool_t rv, ever;
   sigset_t osigmask;
   NYD2_ENTER;

   memset(&gec, 0, sizeof gec);
   osigmask = gcp->gc_osigmask;
   hadint = FAL0;

   if((soldhdl = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN){
      safe_signal(SIGINT, &a_go__eloopint);
      if(sigsetjmp(gcp->gc_eloop_jmp, 1)){
         hold_all_sigs();
         hadint = TRU1;
         a_go_xcall = NULL;
         gcp->gc_flags &= ~a_GO_XCALL_LOOP;
         goto jjump;
      }
   }

   rv = TRU1;
   for(ever = FAL0;; ever = TRU1){
      char const *beoe;
      int n;

      if(ever)
         n_memory_reset();

      /* Read a line of commands and handle end of file specially */
      gec.gec_line.l = gec.gec_line_size;
      rele_all_sigs();
      n = n_go_input(gif, NULL, &gec.gec_line.s, &gec.gec_line.l, NULL);
      hold_all_sigs();
      gec.gec_line_size = (ui32_t)gec.gec_line.l;
      gec.gec_line.l = (ui32_t)n;

      if(n < 0)
         break;

      rele_all_sigs();
      n = a_go_evaluate(&gec);
      hold_all_sigs();

      if(a_go_xcall != NULL)
         break;

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
jjump: /* TODO Should be _CLEANUP_UNWIND not _TEARDOWN on signal if DOABLE! */
   a_go_cleanup(a_GO_CLEANUP_TEARDOWN | (rv ? 0 : a_GO_CLEANUP_ERROR) |
      (hadint ? a_GO_CLEANUP_SIGINT : 0) | a_GO_CLEANUP_HOLDALLSIGS);

   if(gec.gec_line.s != NULL)
      n_free(gec.gec_line.s);

   if(soldhdl != SIG_IGN)
      safe_signal(SIGINT, soldhdl);
   NYD2_LEAVE;
   rele_all_sigs();
   if(hadint){
      sigprocmask(SIG_SETMASK, &osigmask, NULL);
      n_raise(SIGINT);
   }
   return rv;
}

static int
a_go_c_read(void *v){ /* TODO IFS? how? -r */
   struct n_sigman sm;
   char const **argv, *cp, *cp2;
   char *linebuf;
   size_t linesize;
   int rv;
   NYD2_ENTER;

   rv = 0;
   linesize = 0;
   linebuf = NULL;
   argv = v;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      rv = 1;
      goto jleave;
   }
   rv = n_go_input(((n_pstate & n_PS_COMPOSE_MODE
            ? n_GO_INPUT_CTX_COMPOSE : n_GO_INPUT_CTX_DEFAULT) |
         n_GO_INPUT_FORCE_STDIN | n_GO_INPUT_NL_ESC |
         n_GO_INPUT_PROMPT_NONE /* XXX POSIX: PS2: yes! */),
         NULL, &linebuf, &linesize, NULL);
   if(rv < 0)
      goto jleave;

   if(rv > 0){
      cp = linebuf;

      for(rv = 0; *argv != NULL; ++argv){
         char c;

         while(spacechar(*cp))
            ++cp;
         if(*cp == '\0')
            break;

         /* The last variable gets the remaining line less trailing IFS */
         if(argv[1] == NULL){
            for(cp2 = cp; *cp2 != '\0'; ++cp2)
               ;
            for(; cp2 > cp; --cp2){
               c = cp2[-1];
               if(!spacechar(c))
                  break;
            }
         }else
            for(cp2 = cp; (c = *++cp2) != '\0';)
               if(spacechar(c))
                  break;

         /* C99 xxx This is a CC warning workaround (-Wbad-function-cast) */{
            char *vcp;

            vcp = savestrbuf(cp, PTR2SIZE(cp2 - cp));
            if(!a_go__read_set(*argv, vcp)){
               rv = 1;
               break;
            }
         }

         cp = cp2;
      }
   }

   /* Set the remains to the empty string */
   for(; *argv != NULL; ++argv)
      if(!a_go__read_set(*argv, n_empty)){
         rv = 1;
         break;
      }

   if(rv == 0)
      n_pstate_var__em = n_0;

   n_sigman_cleanup_ping(&sm);
jleave:
   if(linebuf != NULL)
      n_free(linebuf);
   NYD2_LEAVE;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

static bool_t
a_go__read_set(char const *cp, char const *value){
   bool_t rv;
   NYD2_ENTER;

   if(!n_shexp_is_valid_varname(cp))
      value = N_("not a valid variable name");
   else if(!n_var_is_user_writable(cp))
      value = N_("variable is read-only");
   else if(!n_var_vset(cp, (uintptr_t)value))
      value = N_("failed to update variable value");
   else{
      rv = TRU1;
      goto jleave;
   }
   n_err("`read': %s: %s\n", V_(value), n_shexp_quote_cp(cp, FAL0));
   rv = FAL0;
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

FL void
n_go_init(void){
   struct a_go_ctx *gcp;
   NYD2_ENTER;

   assert(n_stdin != NULL);

   gcp = (void*)a_go__mainctx_b.uf;
   DBGOR( memset(gcp, 0, n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name)),
      memset(&gcp->gc_data, 0, sizeof gcp->gc_data) );
   gcp->gc_file = n_stdin;
   memcpy(gcp->gc_name, a_GO_MAINCTX_NAME, sizeof(a_GO_MAINCTX_NAME));
   a_go_ctx = gcp;
   n_go_data = &gcp->gc_data;

   n_child_manager_start();
   NYD2_LEAVE;
}

FL bool_t
n_go_main_loop(void){ /* FIXME */
   struct a_go_eval_ctx gec;
   int n, eofcnt;
   bool_t volatile rv;
   NYD_ENTER;

   rv = TRU1;

   if (!(n_pstate & n_PS_SOURCING)) {
      if (safe_signal(SIGINT, SIG_IGN) != SIG_IGN)
         safe_signal(SIGINT, &a_go_onintr);
      if (safe_signal(SIGHUP, SIG_IGN) != SIG_IGN)
         safe_signal(SIGHUP, &a_go_hangup);
   }
   a_go_oldpipe = safe_signal(SIGPIPE, SIG_IGN);
   safe_signal(SIGPIPE, a_go_oldpipe);

   memset(&gec, 0, sizeof gec);

   (void)sigsetjmp(a_go_srbuf, 1); /* FIXME get rid */
   for (eofcnt = 0;; gec.gec_ever_seen = TRU1) {
      interrupts = 0;

      if(gec.gec_ever_seen)
         a_go_cleanup(a_GO_CLEANUP_LOOPTICK);

      if (!(n_pstate & n_PS_SOURCING)) {
         char *cp;

         /* TODO Note: this buffer may contain a password.  We should redefine
          * TODO the code flow which has to do that */
         if ((cp = termios_state.ts_linebuf) != NULL) {
            termios_state.ts_linebuf = NULL;
            termios_state.ts_linesize = 0;
            n_free(cp); /* TODO pool give-back */
         }
         if (gec.gec_line.l > LINESIZE * 3) {
            n_free(gec.gec_line.s);
            gec.gec_line.s = NULL;
            gec.gec_line.l = gec.gec_line_size = 0;
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
               dot = &message[odot];
               n_pstate |= odid;
            }
         }

         n_exit_status = n_EXIT_OK;
      }

      /* Read a line of commands and handle end of file specially */
      gec.gec_line.l = gec.gec_line_size;
      n = n_go_input(n_GO_INPUT_CTX_DEFAULT | n_GO_INPUT_NL_ESC, NULL,
            &gec.gec_line.s, &gec.gec_line.l, NULL);
      gec.gec_line_size = (ui32_t)gec.gec_line.l;
      gec.gec_line.l = (ui32_t)n;

      if (n < 0) {
         if (!(n_pstate & n_PS_ROBOT) &&
               (n_psonce & n_PSO_INTERACTIVE) && ok_blook(ignoreeof) &&
               ++eofcnt < 4) {
            fprintf(n_stdout, _("*ignoreeof* set, use `quit' to quit.\n"));
            n_go_input_clearerr();
            continue;
         }
         break;
      }

      n_pstate &= ~n_PS_HOOK_MASK;
      /* C99 */{
         char const *beoe;
         int estat;

         estat = a_go_evaluate(&gec);
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
               if(!(a_go_ctx->gc_flags & a_GO_TYPE_MASK) ||
                     !(a_go_ctx->gc_flags & a_GO_MACRO_X_OPTION)){
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
            gec.gec_add_history)
         n_tty_addhist(gec.gec_line.s, (gec.gec_add_history != TRU1));

      if(n_pstate & n_PS_EXIT)
         break;
   }

   a_go_cleanup(a_GO_CLEANUP_TEARDOWN | (rv ? 0 : a_GO_CLEANUP_ERROR));

   if (gec.gec_line.s != NULL)
      n_free(gec.gec_line.s);
   NYD_LEAVE;
   return rv;
}

FL void
n_go_input_clearerr(void){
   FILE *fp;
   NYD2_ENTER;

   fp = NULL;

   if(!(a_go_ctx->gc_flags & (a_GO_FORCE_EOF |
         a_GO_PIPE | a_GO_MACRO | a_GO_SPLICE)))
      fp = a_go_ctx->gc_file;

   if(fp != NULL)
      clearerr(fp);
   NYD2_LEAVE;
}

FL void
n_go_input_force_eof(void){
   NYD_ENTER;
   a_go_ctx->gc_flags |= a_GO_FORCE_EOF;
   NYD_LEAVE;
}

FL void
n_go_input_inject(enum n_go_input_inject_flags giif, char const *buf,
      size_t len){
   NYD_ENTER;
   if(len == UIZ_MAX)
      len = strlen(buf);

   if(UIZ_MAX - n_VSTRUCT_SIZEOF(struct a_go_input_inject, gii_dat) -1 > len &&
         len > 0){
      size_t i;
      struct a_go_input_inject *giip,  **giipp;

      hold_all_sigs();

      giip = n_alloc(n_VSTRUCT_SIZEOF(struct a_go_input_inject, gii_dat
            ) + 1 + len +1);
      giipp = &a_go_ctx->gc_inject;
      giip->gii_next = *giipp;
      giip->gii_commit = ((giif & n_GO_INPUT_INJECT_COMMIT) != 0);
      if(buf[i = 0] != ' ' && !(giif & n_GO_INPUT_INJECT_HISTORY))
         giip->gii_dat[i++] = ' '; /* TODO prim. hack to avoid history put! */
      memcpy(&giip->gii_dat[i], buf, len);
      i += len;
      giip->gii_dat[giip->gii_len = i] = '\0';
      *giipp = giip;

      rele_all_sigs();
   }
   NYD_LEAVE;
}

FL int
(n_go_input)(enum n_go_input_flags gif, char const *prompt, char **linebuf,
      size_t *linesize, char const *string n_MEMORY_DEBUG_ARGS){
   /* TODO readline: linebuf pool!; n_go_input should return si64_t */
   struct n_string xprompt;
   FILE *ifile;
   bool_t doprompt, dotty;
   char const *iftype;
   int nold, n;
   NYD2_ENTER;

   if(!(gif & n_GO_INPUT_HOLDALLSIGS))
      hold_all_sigs();

   if(a_go_ctx->gc_flags & a_GO_FORCE_EOF){
      n = -1;
      goto jleave;
   }

   if(gif & n_GO_INPUT_FORCE_STDIN)
      goto jforce_stdin;

   /* Special case macro mode: never need to prompt, lines have always been
    * unfolded already */
   if(a_go_ctx->gc_flags & a_GO_MACRO){
      struct a_go_input_inject *giip;

      if(*linebuf != NULL)
         n_free(*linebuf);

      /* Injection in progress?  Don't care about the autocommit state here */
      if((giip = a_go_ctx->gc_inject) != NULL){
         a_go_ctx->gc_inject = giip->gii_next;

         *linesize = giip->gii_len;
         *linebuf = (char*)giip;
         memmove(*linebuf, giip->gii_dat, giip->gii_len +1);
         iftype = "INJECTION";
      }else{
         if((*linebuf = a_go_ctx->gc_lines[a_go_ctx->gc_loff]) == NULL){
            *linesize = 0;
            n = -1;
            goto jleave;
         }

         ++a_go_ctx->gc_loff;
         *linesize = strlen(*linebuf);
         if(!(a_go_ctx->gc_flags & a_GO_MACRO_FREE_DATA))
            *linebuf = sbufdup(*linebuf, *linesize);

         iftype = (a_go_ctx->gc_flags & a_GO_MACRO_X_OPTION)
               ? "-X OPTION"
               : (a_go_ctx->gc_flags & a_GO_MACRO_CMD) ? "CMD" : "MACRO";
      }
      n = (int)*linesize;
      n_pstate |= n_PS_READLINE_NL;
      goto jhave_dat;
   }else{
      /* Injection in progress? */
      struct a_go_input_inject **giipp, *giip;

      giipp = &a_go_ctx->gc_inject;

      if((giip = *giipp) != NULL){
         *giipp = giip->gii_next;

         if(giip->gii_commit){
            if(*linebuf != NULL)
               n_free(*linebuf);

            /* Simply reuse the buffer */
            n = (int)(*linesize = giip->gii_len);
            *linebuf = (char*)giip;
            memmove(*linebuf, giip->gii_dat, giip->gii_len +1);
            iftype = "INJECTION";
            n_pstate |= n_PS_READLINE_NL;
            goto jhave_dat;
         }else{
            string = savestrbuf(giip->gii_dat, giip->gii_len);
            n_free(giip);
         }
      }
   }

jforce_stdin:
   n_pstate &= ~n_PS_READLINE_NL;
   iftype = (!(n_psonce & n_PSO_STARTED) ? "LOAD"
          : (n_pstate & n_PS_SOURCING) ? "SOURCE" : "READ");
   doprompt = ((n_psonce & (n_PSO_INTERACTIVE | n_PSO_STARTED)) ==
         (n_PSO_INTERACTIVE | n_PSO_STARTED) && !(n_pstate & n_PS_ROBOT));
   dotty = (doprompt && !ok_blook(line_editor_disable));
   if(!doprompt)
      gif |= n_GO_INPUT_PROMPT_NONE;
   else{
      if(!dotty)
         n_string_creat_auto(&xprompt);
      if(prompt == NULL)
         gif |= n_GO_INPUT_PROMPT_EVAL;
   }

   /* Ensure stdout is flushed first anyway (partial lines, maybe?) */
   if(!dotty && (gif & n_GO_INPUT_PROMPT_NONE))
      fflush(n_stdout);

   ifile = (gif & n_GO_INPUT_FORCE_STDIN) ? n_stdin : a_go_ctx->gc_file;
   if(ifile == NULL){
      assert((n_pstate & n_PS_COMPOSE_FORKHOOK) &&
         (a_go_ctx->gc_flags & a_GO_MACRO));
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

         rele_all_sigs();

         n = (n_tty_readline)(gif, prompt, linebuf, linesize, n
               n_MEMORY_DEBUG_ARGSCALL);

         hold_all_sigs();
      }else{
         if(!(gif & n_GO_INPUT_PROMPT_NONE))
            n_tty_create_prompt(&xprompt, prompt, gif);

         rele_all_sigs();

         if(!(gif & n_GO_INPUT_PROMPT_NONE) && xprompt.s_len > 0){
            fwrite(xprompt.s_dat, 1, xprompt.s_len, n_stdout);
            fflush(n_stdout);
         }

         n = (readline_restart)(ifile, linebuf, linesize, n
               n_MEMORY_DEBUG_ARGSCALL);

         hold_all_sigs();

         if(n > 0 && nold > 0){
            char const *cp;
            int i;

            i = 0;
            cp = &(*linebuf)[nold];
            while(spacechar(*cp) && n - i >= nold)
               ++cp, ++i;
            if(i > 0){
               memmove(&(*linebuf)[nold], cp, n - nold - i);
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
      if(!(gif & n_GO_INPUT_NL_ESC) || (*linebuf)[n - 1] != '\\'){
         if(dotty)
            n_pstate |= n_PS_READLINE_NL;
         break;
      }
      /* Definitely outside of quotes, thus the quoting rules are so that an
       * uneven number of successive reverse solidus at EOL is a continuation */
      if(n > 1){
         size_t i, j;

         for(j = 1, i = (size_t)n - 1; i-- > 0; ++j)
            if((*linebuf)[i] != '\\')
               break;
         if(!(j & 1))
            break;
      }
      (*linebuf)[nold = --n] = '\0';
      gif |= n_GO_INPUT_NL_FOLLOW;
   }

   if(n < 0)
      goto jleave;
   (*linebuf)[*linesize = n] = '\0';

jhave_dat:
#if 0
   if(gif & n_GO_INPUT_DROP_TRAIL_SPC){
      char *cp, c;
      size_t i;

      for(cp = &(*linebuf)[i = (size_t)n];; --i){
         c = *--cp;
         if(!spacechar(c))
            break;
      }
      (*linebuf)[n = (int)i] = '\0';
   }

   if(gif & n_GO_INPUT_DROP_LEAD_SPC){
      char *cp, c;
      size_t j, i;

      for(cp = &(*linebuf)[0], j = (size_t)n, i = 0; i < j; ++i){
         c = *cp++;
         if(!spacechar(c))
            break;
      }
      if(i > 0){
         memmove(&(*linebuf)[0], &(*linebuf)[i], j -= i);
         (*linebuf)[n = (int)j] = '\0';
      }
   }
#endif /* 0 (notyet - must take care for reverse solidus escaped space) */

   if(n_poption & n_PO_D_VV)
      n_err(_("%s %d bytes <%s>\n"), iftype, n, *linebuf);
jleave:
   if (n_pstate & n_PS_PSTATE_PENDMASK)
      a_go_update_pstate();

   /* TODO We need to special case a_GO_SPLICE, since that is not managed by us
    * TODO but only established from the outside and we need to drop this
    * TODO overlay context somehow */
   if(n < 0 && (a_go_ctx->gc_flags & a_GO_SPLICE))
      a_go_cleanup(a_GO_CLEANUP_TEARDOWN | a_GO_CLEANUP_HOLDALLSIGS);

   if(!(gif & n_GO_INPUT_HOLDALLSIGS))
      rele_all_sigs();
   NYD2_LEAVE;
   return n;
}

FL char *
n_go_input_cp(enum n_go_input_flags gif, char const *prompt,
      char const *string){
   struct n_sigman sm;
   size_t linesize;
   char *linebuf, * volatile rv;
   int n;
   NYD2_ENTER;

   linesize = 0;
   linebuf = NULL;
   rv = NULL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      goto jleave;
   }

   n = n_go_input(gif, prompt, &linebuf, &linesize, string);
   if(n > 0 && *(rv = savestrbuf(linebuf, (size_t)n)) != '\0' &&
         (gif & n_GO_INPUT_HIST_ADD) && (n_psonce & n_PSO_INTERACTIVE))
      n_tty_addhist(rv, ((gif & n_GO_INPUT_HIST_GABBY) != 0));

   n_sigman_cleanup_ping(&sm);
jleave:
   if(linebuf != NULL)
      n_free(linebuf);
   NYD2_LEAVE;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

FL void
n_go_load(char const *name){
   struct a_go_ctx *gcp;
   size_t i;
   FILE *fip;
   NYD_ENTER;

   if(name == NULL || *name == '\0')
      goto jleave;
   else if((fip = Fopen(name, "r")) == NULL){
      if(n_poption & n_PO_D_V)
         n_err(_("No such file to load: %s\n"), n_shexp_quote_cp(name, FAL0));
      goto jleave;
   }

   i = strlen(name) +1;
   gcp = n_alloc(n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name) + i);
   memset(gcp, 0, n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name));

   gcp->gc_file = fip;
   gcp->gc_flags = a_GO_FREE | a_GO_FILE;
   memcpy(gcp->gc_name, name, i);

   if(n_poption & n_PO_D_V)
      n_err(_("Loading %s\n"), n_shexp_quote_cp(gcp->gc_name, FAL0));
   a_go_load(gcp);
   n_pstate &= ~n_PS_EXIT;
jleave:
   NYD_LEAVE;
}

FL void
n_go_Xargs(char const **lines, size_t cnt){
   static char const name[] = "-X";

   union{
      ui64_t align;
      char uf[n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name) + sizeof(name)];
   } b;
   char const *srcp, *xsrcp;
   char *cp;
   size_t imax, i, len;
   struct a_go_ctx *gcp;
   NYD_ENTER;

   gcp = (void*)b.uf;
   memset(gcp, 0, n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name));

   gcp->gc_flags = a_GO_MACRO | a_GO_MACRO_X_OPTION |
         a_GO_SUPER_MACRO | a_GO_MACRO_FREE_DATA;
   memcpy(gcp->gc_name, name, sizeof name);

   /* The problem being that we want to support reverse solidus newline
    * escaping also within multiline -X, i.e., POSIX says:
    *    An unquoted <backslash> at the end of a command line shall
    *    be discarded and the next line shall continue the command
    * Therefore instead of "gcp->gc_lines = n_UNCONST(lines)", duplicate the
    * entire lines array and set _MACRO_FREE_DATA */
   imax = cnt + 1;
   gcp->gc_lines = n_alloc(sizeof(*gcp->gc_lines) * imax);

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
         while(j > 0 && spacechar(*srcp))
            ++srcp, --j;

      if(j > 0){
         if(i + 2 >= imax){ /* TODO need a vector (main.c, here, ++) */
            imax += 4;
            gcp->gc_lines = n_realloc(gcp->gc_lines, sizeof(*gcp->gc_lines) *
                  imax);
         }
         gcp->gc_lines[i] = cp = n_realloc(cp, len + j +1);
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
      gcp->gc_lines[i++] = cp;
   }
   gcp->gc_lines[i] = NULL;

   a_go_load(gcp);
   if(n_pstate & n_PS_EXIT)
      exit(n_exit_status);
   NYD_LEAVE;
}

FL int
c_source(void *v){
   int rv;
   NYD_ENTER;

   rv = (a_go_file(*(char**)v, FAL0) == TRU1) ? 0 : 1;
   NYD_LEAVE;
   return rv;
}

FL int
c_source_if(void *v){ /* XXX obsolete?, support file tests in `if' etc.! */
   int rv;
   NYD_ENTER;

   rv = (a_go_file(*(char**)v, TRU1) == TRU1) ? 0 : 1;
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_go_macro(enum n_go_input_flags gif, char const *name, char **lines,
      void (*on_finalize)(void*), void *finalize_arg){
   struct a_go_ctx *gcp;
   size_t i;
   int rv;
   sigset_t osigmask;
   NYD_ENTER;

   sigprocmask(SIG_BLOCK, NULL, &osigmask);

   gcp = n_alloc(n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name) +
         (i = strlen(name) +1));
   memset(gcp, 0, n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name));

   hold_all_sigs();

   gcp->gc_outer = a_go_ctx;
   gcp->gc_osigmask = osigmask;
   gcp->gc_flags = a_GO_FREE | a_GO_MACRO | a_GO_MACRO_FREE_DATA |
         ((!(a_go_ctx->gc_flags & a_GO_TYPE_MASK) ||
            (a_go_ctx->gc_flags & a_GO_SUPER_MACRO)) ? a_GO_SUPER_MACRO : 0);
   gcp->gc_lines = lines;
   gcp->gc_on_finalize = on_finalize;
   gcp->gc_finalize_arg = finalize_arg;
   memcpy(gcp->gc_name, name, i);

   a_go_ctx = gcp;
   n_go_data = &gcp->gc_data;
   n_pstate |= n_PS_ROBOT;
   rv = a_go_event_loop(gcp, gif);

   /* Shall this enter a `xcall' stack avoidance optimization (loop)? */
   if(a_go_xcall != NULL){
      if(a_go_xcall == (struct a_go_xcall*)-1)
         a_go_xcall = NULL;
      else if(a_go_xcall->gx_upto == gcp){
         /* Indicate that "our" (ex-) parent now hosts xcall optimization */
         a_go_ctx->gc_flags |= a_GO_XCALL_LOOP;
         while(a_go_xcall != NULL){
            char *cp, **argv;
            struct a_go_xcall *gxp;

            hold_all_sigs();

            gxp = a_go_xcall;
            a_go_xcall = NULL;

            /* Recreate the ARGV of this command on the LOFI memory of the
             * hosting a_go_ctx, so that it will become auto-reclaimed */
            /* C99 */{
               void *vp;

               vp = n_lofi_alloc(gxp->gx_buflen);
               cp = vp;
               argv = vp;
            }
            cp += sizeof(*argv) * (gxp->gx_argc + 1);
            for(i = 0; i < gxp->gx_argc; ++i){
               argv[i] = cp;
               memcpy(cp, gxp->gx_argv[i].s, gxp->gx_argv[i].l +1);
               cp += gxp->gx_argv[i].l +1;
            }
            argv[i] = NULL;
            n_free(gxp);

            rele_all_sigs();

            rv = (c_call(argv) == 0);

            n_lofi_free(argv);
         }
         a_go_ctx->gc_flags &= ~a_GO_XCALL_LOOP;
      }
   }
   NYD_LEAVE;
   return rv;
}

FL bool_t
n_go_command(enum n_go_input_flags gif, char const *cmd){
   struct a_go_ctx *gcp;
   bool_t rv;
   size_t i, ial;
   sigset_t osigmask;
   NYD_ENTER;

   sigprocmask(SIG_BLOCK, NULL, &osigmask);

   i = strlen(cmd) +1;
   ial = n_ALIGN(i);
   gcp = n_alloc(n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name) +
         ial + 2*sizeof(char*));
   memset(gcp, 0, n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name));

   hold_all_sigs();

   gcp->gc_outer = a_go_ctx;
   gcp->gc_osigmask = osigmask;
   gcp->gc_flags = a_GO_FREE | a_GO_MACRO | a_GO_MACRO_CMD |
         ((!(a_go_ctx->gc_flags & a_GO_TYPE_MASK) ||
            (a_go_ctx->gc_flags & a_GO_SUPER_MACRO)) ? a_GO_SUPER_MACRO : 0);
   gcp->gc_lines = (void*)&gcp->gc_name[ial];
   memcpy(gcp->gc_lines[0] = &gcp->gc_name[0], cmd, i);
   gcp->gc_lines[1] = NULL;

   a_go_ctx = gcp;
   n_go_data = &gcp->gc_data;
   n_pstate |= n_PS_ROBOT;
   rv = a_go_event_loop(gcp, gif);
   NYD_LEAVE;
   return rv;
}

FL void
n_go_splice_hack(char const *cmd, FILE *new_stdin, FILE *new_stdout,
      ui32_t new_psonce, void (*on_finalize)(void*), void *finalize_arg){
   struct a_go_ctx *gcp;
   size_t i;
   sigset_t osigmask;
   NYD_ENTER;

   sigprocmask(SIG_BLOCK, NULL, &osigmask);

   gcp = n_alloc(n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name) +
         (i = strlen(cmd) +1));
   memset(gcp, 0, n_VSTRUCT_SIZEOF(struct a_go_ctx, gc_name));

   hold_all_sigs();

   gcp->gc_outer = a_go_ctx;
   gcp->gc_osigmask = osigmask;
   gcp->gc_file = new_stdin;
   gcp->gc_flags = a_GO_FREE | a_GO_SPLICE;
   gcp->gc_on_finalize = on_finalize;
   gcp->gc_finalize_arg = finalize_arg;
   gcp->gc_splice_stdin = n_stdin;
   gcp->gc_splice_stdout = n_stdout;
   gcp->gc_splice_psonce = n_psonce;
   memcpy(gcp->gc_name, cmd, i);

   n_stdin = new_stdin;
   n_stdout = new_stdout;
   n_psonce = new_psonce;
   a_go_ctx = gcp;
   n_pstate |= n_PS_ROBOT;

   rele_all_sigs();
   NYD_LEAVE;
}

FL void
n_go_splice_hack_remove_after_jump(void){
   a_go_cleanup(a_GO_CLEANUP_TEARDOWN);
}

FL bool_t
n_go_may_yield_control(void){ /* TODO this is a terrible hack */
   struct a_go_ctx *gcp;
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
   for(gcp = a_go_ctx; gcp != NULL; gcp = gcp->gc_outer){
      if(gcp->gc_flags & (a_GO_PIPE | a_GO_FILE | a_GO_SPLICE))
         goto jleave;
   }

   rv = TRU1;
jleave:
   NYD2_LEAVE;
   return rv;
}

/* s-it-mode */
