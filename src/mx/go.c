/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of go.h.
 *@ TODO - everything should take a_go_ctx* and work through it.
 *@ TODO - _PS_ERR_EXIT_* and _PSO_EXIT_* mixup is a mess: TERRIBLE!
 *@ TODO - sigs_hold_all() most often on, especially robot mode: TERRIBLE!
 *@ TODO - go_input(): with IO::Device we could have CStringListDevice, for
 *@ TODO   example to handle injections, and also `readctl' channels!
 *@ TODO   (Including sh(1)ell HERE strings and such.)
 *
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#undef su_FILE
#define su_FILE go
#define mx_SOURCE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/cs.h>
#include <su/cs-dict.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#include "mx/child.h"
#include "mx/cmd.h"
#include "mx/cmd-cnd.h"
#include "mx/cmd-commandalias.h"
#include "mx/colour.h"
#include "mx/dig-msg.h"
#include "mx/file-streams.h"
#include "mx/sigs.h"
#include "mx/termios.h"
#include "mx/tty.h"
#include "mx/ui-str.h"

#include "mx/go.h"
#include "su/code-in.h"

enum a_go_flags{
   a_GO_NONE,
   a_GO_FREE = 1u<<0, /* Structure was allocated, FREE() it */
   a_GO_PIPE = 1u<<1, /* Open on a pipe */
   a_GO_FILE = 1u<<2, /* Loading or sourcing a file */
   a_GO_MACRO = 1u<<3, /* Running a macro */
   a_GO_MACRO_FREE_DATA = 1u<<4, /* Lines are allocated, FREE() once done */
   /* TODO For simplicity this is yet _MACRO plus specialization overlay
    * TODO (_X_OPTION, _BLTIN_RC, _CMD) -- should be types on their own! */
   a_GO_MACRO_X_OPTION = 1u<<5, /* Macro indeed command line -X option */
   a_GO_MACRO_BLTIN_RC = 1u<<6, /* Macro indeed command line -:x option */
   a_GO_MACRO_CMD = 1u<<7, /* Macro indeed single-line: ~:COMMAND */
   /* TODO a_GO_SPLICE: the right way to support *on-compose-splice(-shell)?*
    * TODO would be a command_loop object that emits an on_read_line event, and
    * TODO have a special handler for the compose mode; with that, then,
    * TODO _event_loop() would not call _evaluate() but CTX->on_read_line,
    * TODO and _evaluate() would be the standard impl.,
    * TODO whereas the COMMAND ESCAPE switch in collect.c would be another one.
    * TODO With this generic accmacvar.c:temporary_compose_mode_hook_call()
    * TODO could be dropped, and go_macro() could become extended,
    * TODO and/or we would add a go_anything(), which would allow special
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
   a_GO_SPLICE = 1u<<8,
   /* If it is none of those, it must be the outermost, the global one */
   a_GO_TYPE_MASK = a_GO_PIPE | a_GO_FILE | a_GO_MACRO |
         /* a_GO_MACRO_X_OPTION | a_GO_MACRO_BLTIN_RC | a_GO_MACRO_CMD | */
         a_GO_SPLICE,

   a_GO_TYPE_MACRO_MASK = a_GO_MACRO | a_GO_MACRO_X_OPTION |
         a_GO_MACRO_BLTIN_RC | a_GO_MACRO_CMD,

   a_GO_FORCE_EOF = 1u<<14, /* go_input() shall return EOF next */
   a_GO_IS_EOF = 1u<<15,

   /* We are down the call chain of a .. robot (Combinable) */
   a_GO_MACRO_ROBOT = 1u<<16,
   a_GO_FILE_ROBOT = 1u<<17,
   /* This context has inherited the memory bag from its parent.
    * In practice only used for resource file loading and -X args, which enter
    * a top level go_main_loop() and should (re)use the in practice already
    * allocated memory bag of the global context.
    * The bag memory is reset after used */
   a_GO_MEMBAG_INHERITED = 1u<<18,
   /* This context has inherited the entire data context from its parent */
   a_GO_DATACTX_INHERITED = 1u<<19,

   /* GO_INPUT_NO_XCALL: `xcall' should be like `call' (except rest of macro is
    * not evaluated) */
   a_GO_XCALL_IS_CALL = 1u<<24,
   /* `xcall' optimization barrier: go_macro() has been finished with a `xcall'
    * request, and "`xcall' set this in the parent" _go_input of the said
    * go_macro() to indicate a barrier: we teardown the _go_input of the
    * go_macro() away after leaving its _event_loop(), but then, back in
    * go_macro(), that enters a for(;;) loop that directly calls c_call() --
    * our `xcall' stack avoidance optimization --, yet this call will itself
    * end up in a new go_macro(), and if that again ends up with `xcall' this
    * should teardown and leave its own go_macro(), unrolling the stack "up to
    * the barrier level", but which effectively still is the go_macro() that
    * lost its _go_input and is looping the `xcall' optimization loop.  If no
    * `xcall' is desired that loop is simply left and the _event_loop() of the
    * outer _go_ctx will perform a loop tick and clear this bit again OR become
    * teardown itself */
   a_GO_XCALL_LOOP = 1u<<25,
   a_GO_XCALL_LOOP_ERROR = 1u<<26, /* .. state machine error transporter */
   a_GO_XCALL_LOOP_MASK = a_GO_XCALL_LOOP | a_GO_XCALL_LOOP_ERROR
};

enum a_go_cleanup_mode{
   a_GO_CLEANUP_UNWIND = 1u<<0, /* Teardown all ctxs except outermost */
   a_GO_CLEANUP_TEARDOWN = 1u<<1, /* Teardown current context */
   a_GO_CLEANUP_LOOPTICK = 1u<<2, /* Normal looptick cleanup */
   a_GO_CLEANUP_MODE_MASK = su_BITENUM_MASK(0, 2),

   a_GO_CLEANUP_ERROR = 1u<<8, /* Error occurred on level */
   a_GO_CLEANUP_SIGINT = 1u<<9, /* Interrupt signal received */
   a_GO_CLEANUP_HOLDALLSIGS = 1u<<10 /* sigs_all_hold() active TODO */
};

enum a_go_hist_flags{
   a_GO_HIST_NONE = 0,
   a_GO_HIST_ADD = 1u<<0,
   a_GO_HIST_GABBY = 1u<<1,
   a_GO_HIST_GABBY_ERROR = 1u<<2,
   a_GO_HIST_INIT = 1u<<3
};

struct a_go_eval_ctx{
   struct str gec_line; /* The terminated data to _evaluate() */
   u32 gec_line_size; /* May be used to store line memory size */
   boole gec_ever_seen; /* Has ever been used (main_loop() only) */
   boole gec_ignerr; /* Implicit `ignerr' prefix */
   u8 gec__dummy[1];
   u8 gec_hist_flags; /* enum a_go_hist_flags */
   char const *gec_hist_cmd; /* If _GO_HIST_ADD only, cmd and args */
   char const *gec_hist_args;
};

struct a_go_input_inject{
   struct a_go_input_inject *gii_next;
   uz gii_len;
   boole gii_commit;
   boole gii_no_history;
   char gii_dat[VFIELD_SIZE(6)];
};

struct a_go_ctx{
   struct a_go_ctx *gc_outer;
   sigset_t gc_osigmask;
   BITENUM_IS(u32,a_go_flags) gc_flags;
   u32 gc_loff; /* Pseudo (macro): index in .gc_lines */
   char **gc_lines; /* Pseudo content, lines unfolded */
   FILE *gc_file; /* File we were in, if applicable */
   struct a_go_input_inject *gc_inject; /* To be consumed first */
   void (*gc_on_finalize)(void *);
   void *gc_finalize_arg;
   /* When `xcall' is used to replace the running macro we `localopts' must be
    * carried over to the macro running next: stored here */
   void *gc_xcall_lopts;
   sigjmp_buf gc_eloop_jmp; /* TODO one day...  for _event_loop() */
   /* SPLICE hacks: saved stdin/stdout, saved pstate */
   FILE *gc_splice_stdin;
   FILE *gc_splice_stdout;
   u32 gc_splice_psonce;
   u8 gc_splice__dummy[4];
   struct mx_go_data_ctx gc_data;
   char gc_name[VFIELD_SIZE(0)]; /* Name of file or macro */
};

struct a_go_readctl_ctx{ /* TODO localize readctl_read_overlay: OnForkEvent! */
   struct a_go_readctl_ctx *grc_last;
   struct a_go_readctl_ctx *grc_next;
   char const *grc_expand; /* If filename based, expanded string */
   FILE *grc_fp;
   s32 grc_fd; /* Based upon file-descriptor */
   char grc_name[VFIELD_SIZE(4)]; /* User input for identification purposes */
};

static char const * const a_go_bltin_rc_lines[] = {
#include "gen-bltin-rc.h" /* */
};

static n_sighdl_t a_go_oldpipe;

/* Our current execution context, and the buffer backing the outermost level */
static struct a_go_ctx *a_go_ctx;

#define a_GO_MAINCTX_NAME "top level/main loop"
static union{
   u64 align;
   char uf[VSTRUCT_SIZEOF(struct a_go_ctx,gc_name) +
         sizeof(a_GO_MAINCTX_NAME)];
} a_go__mainctx_b;

/* `xcall' stack-avoidance bypass optimization.  This actually is
 * a cmd_arg_save_to_heap() buffer with cmd_arg_ctx.cac_indat misused to
 * point to the _go_ctx to unroll up to */
static void *a_go_xcall; /* XXX without global data! store in context! */

static sigjmp_buf a_go_srbuf; /* TODO GET RID */

struct mx_go_data_ctx *mx_go_data;

/* PS_STATE_PENDMASK requires some actions */
static void a_go_update_pstate(void);

/* Evaluate a single command */
static boole a_go_evaluate(struct a_go_ctx *gcp, struct a_go_eval_ctx *gecp);

/* Branch here on hangup signal and simulate "exit" */
static void a_go_hangup(int s);

/* The following gets called on receipt of an interrupt */
static void a_go_onintr(int s);

/* Cleanup gcp (current execution context; xxx update the program state).
 * If _CLEANUP_ERROR is set then we do not alert and error out if the stack
 * does not exist at all, unless _CLEANUP_HOLDALLSIGS we sigs_all_hold() */
static void a_go_cleanup(struct a_go_ctx *gcp,
      BITENUM_IS(u32,a_go_cleanup_mode) gcm);

/* `source' and `source_if' (if silent_open_error: no pipes allowed, then).
 * Returns FAL0 if file is somehow not usable (unless silent_open_error) or
 * upon evaluation error, and TRU1 on success */
static boole a_go_file(char const *file, boole silent_open_error);

/* System resource file load()ing or -X command line option array traversal */
static boole a_go_load(struct a_go_ctx *gcp);

/* A simplified command loop for recursed state machines */
static boole a_go_event_loop(struct a_go_ctx *gcp,
      BITENUM_IS(u32,mx_go_input_flags) gif);

static void
a_go_update_pstate(void){
   boole act;
   NYD_IN;

   act = ((n_pstate & n_PS_SIGWINCH_PEND) != 0);
   n_pstate &= ~n_PS_PSTATE_PENDMASK;

   if(act){
      char iencbuf[su_IENC_BUFFER_SIZE], *cp;

      cp = su_ienc_u32(iencbuf, mx_termios_dimen.tiosd_real_width, 10);
      ok_vset(COLUMNS, cp);
      cp = su_ienc_u32(iencbuf, mx_termios_dimen.tiosd_real_height, 10);
      ok_vset(LINES, cp);
   }

   NYD_OU;
}

static boole
a_go_evaluate(struct a_go_ctx *gcp, struct a_go_eval_ctx *gecp){
   /* TODO old style(9), but also old code */
   /* TODO a_go_evaluate() should be split in multiple subfunctions,
    * TODO `eval' should be a prefix, etc., a cmd_ctx should be passed along;
    * TODO this also affects history handling, as below!  etc etc */
   struct str line;
   struct n_string s_b, *s;
   char _wordbuf[2], *argv_stack[3], **argv_base, **argvp, *vput, *cp, *word;
   char const *alias_name, *emsg;
   struct mx_cmd_desc const *cdp;
   s32 nerrn, nexn;     /* TODO n_pstate_ex_no -> s64! */
   int rv, c;
   enum{
      a_NONE = 0,
      a_ALIAS_MASK = su_BITENUM_MASK(0, 2), /* Alias recursion counter bits */
      a_NOPREFIX = 1u<<4, /* Modifier prefix not allowed right now */
      a_NOALIAS = 1u<<5, /* "No alias!" expansion modifier */
      a_IGNERR = 1u<<6, /* ignerr modifier prefix */
      a_LOCAL = 1u<<7, /* local modifier prefix */
      a_GLOBAL = 1u<<8, /* TODO global modifier prefix */
      a_U = 1u<<9, /* TODO UTF-8 modifier prefix */
      a_VPUT = 1u<<10, /* vput modifier prefix */
      a_WYSH = 1u<<11, /* XXX v15+ drop wysh modifier prefix */
      a_MODE_MASK = su_BITENUM_MASK(5, 11),
      a_NO_ERRNO = 1u<<16, /* Don't set n_pstate_err_no */
      a_IS_SKIP = 1u<<17, /* Conditional active, is skipping */
      a_IS_EMPTY = 1u<<18 /* The empty command */
   } flags;
   NYD_IN;

   /* Take care not to overwrite existing exit status */
   if(!(n_psonce & n_PSO_EXIT_MASK) && !(n_pstate & n_PS_ERR_EXIT_MASK))
      n_exit_status = n_EXIT_OK;

   flags = ((mx_cnd_if_exists() == TRUM1 ? a_IS_SKIP : a_NONE) |
         (gecp->gec_ignerr ? a_IGNERR : a_NONE));
   rv = 1;
   nerrn = su_ERR_NONE;
   nexn = n_EXIT_OK;
   cdp = NIL;
   vput = NIL;
   alias_name = NIL;
   line = gecp->gec_line; /* TODO const-ify original (buffer)! */
   ASSERT(line.s[line.l] == '\0');

   if(line.l > 0 && su_cs_is_space(line.s[0]))
      gecp->gec_hist_flags = a_GO_HIST_NONE;
   else if(gecp->gec_hist_flags & a_GO_HIST_ADD)
      gecp->gec_hist_cmd = gecp->gec_hist_args = NIL;
   s = NIL;

   /* Aliases that refer to shell commands or macro expansion restart */
jrestart:
   if(n_str_trim_ifs(&line, TRU1)->l == 0){
      line.s[0] = '\0';
      flags |= a_IS_EMPTY;
      cdp = mx_cmd_default();
      gecp->gec_hist_flags = a_GO_HIST_NONE;
      goto jexec;
   }
   (cp = line.s)[line.l] = '\0';

   /* No-expansion modifier? */
   if(!(flags & a_NOPREFIX) && *cp == '\\'){
      line.s = ++cp;
      --line.l;
      flags |= a_NOALIAS;
   }

   /* Note: adding more special treatments must be reflected in the `help'
    * etc. output in cmd.c! */

   /* Ignore null commands (comments) */
   if(*cp == '#'){
      gecp->gec_hist_flags = a_GO_HIST_NONE;
      goto jret0;
   }

   /* Handle ! differently to get the correct lexical conventions */
   if(*cp == '!')
      ++cp;
   /* Isolate the actual command; since it may not necessarily be
    * separated from the arguments (as in `p1') we need to duplicate it to
    * be able to create a NUL terminated version.
    * We must be aware of several special one letter commands here */
   else if((cp = UNCONST(char*,mx_cmd_isolate_name(cp))) == line.s &&
         (*cp == '|' || *cp == '?'))
      ++cp;
   c = S(int,P2UZ(cp - line.s));
   word = UCMP(z, c, <, sizeof _wordbuf) ? _wordbuf : su_AUTO_ALLOC(c +1);
   su_mem_copy(word, line.s, c);
   word[c] = '\0';
   line.l -= c;
   line.s = cp;

   /* It may be a modifier.
    * NOTE: changing modifiers must be reflected in cmd_is_valid_name() */
   switch(c){
   default:
      break;
   /*case sizeof("global") -1:*/
   case sizeof("ignerr") -1:
      if(!su_cs_cmp_case(word, "ignerr")){
         flags |= a_NOPREFIX | a_IGNERR;
         goto jrestart;
      }
      if(!su_cs_cmp_case(word, "global")){
         n_err(_("Ignoring yet unused `global' command modifier!"));
         flags |= a_NOPREFIX | a_GLOBAL;
         goto jrestart;
      }
      break;
   case sizeof("local") -1:
      if(!su_cs_cmp_case(word, "local")){
         flags |= a_NOPREFIX | a_LOCAL;
         goto jrestart;
      }
      break;
   case sizeof("u") -1:
      if(!su_cs_cmp_case(word, "u")){
         n_err(_("Ignoring yet unused `u' command modifier!"));
         flags |= a_NOPREFIX | a_U;
         goto jrestart;
      }
      break;
   /*case sizeof("vput") -1:*/
   case sizeof("wysh") -1:
      if(!su_cs_cmp_case(word, "wysh")){
         flags |= a_NOPREFIX | a_WYSH;
         goto jrestart;
      }else if(!su_cs_cmp_case(word, "vput")){
         flags |= a_NOPREFIX | a_VPUT;
         goto jrestart;
      }
      break;
   }

   /* We need to trim for a possible history entry, but do it anyway and insert
    * a space for argument separation in case of alias expansion.  Also, do
    * terminate again because nothing prevents aliases from introducing WS */
   n_str_trim_ifs(&line, TRU1);
   line.s[line.l] = '\0';

   /* Lengthy history entry setup, possibly even redundant.  But having
    * normalized history entries is a good thing, and this is maybe still
    * cheaper than parsing a StrList of words per se
    * TODO In v15 the history entry will be deduced from the argument vector,
    * TODO possibly modified by the command itself, i.e., from the cmd_ctx
    * TODO structure which is passed along.  And only if we have to do it */
   if((gecp->gec_hist_flags & (a_GO_HIST_ADD | a_GO_HIST_INIT)
         ) == a_GO_HIST_ADD){
      if(line.l > 0){
         s = n_string_creat_auto(&s_b);
         s = n_string_assign_buf(s, line.s, line.l);
         gecp->gec_hist_args = n_string_cp(s);
         /* n_string_gut(n_string_drop_ownership(s)); */
      }

      s = n_string_creat_auto(&s_b);
      s = n_string_reserve(s, 32);

      if(flags & a_NOALIAS)
         s = n_string_push_c(s, '\\');
      if(flags & a_WYSH)
         s = n_string_push_buf(s, "wysh ", sizeof("wysh ") -1);
      if(flags & a_IGNERR)
         s = n_string_push_buf(s, "ignerr ", sizeof("ignerr ") -1);
      if(flags & a_LOCAL)
         s = n_string_push_buf(s, "local ", sizeof("local ") -1);
      if(flags & a_VPUT)
         s = n_string_push_buf(s, "vput ", sizeof("vput ") -1);
      gecp->gec_hist_flags = a_GO_HIST_ADD | a_GO_HIST_INIT;
   }

   /* Look up the command; if not found, bitch.  An empty cmd maps to the first
    * command table entry.. */
   if(*word == '\0'){
      flags |= a_IS_EMPTY;
      cdp = mx_cmd_default();
      goto jexec;
   }

   /* Can we expand an alias from what we have? */
   if(!(flags & a_NOALIAS) && (flags & a_ALIAS_MASK) != a_ALIAS_MASK){
      char const *alias_exp;
      u8 expcnt;

      expcnt = (flags & a_ALIAS_MASK);
      ++expcnt;
      flags = (flags & ~(a_ALIAS_MASK | a_NOPREFIX)) | expcnt;

      /* Avoid self-recursion; since a commandalias can shadow a command of
       * equal name allow one level of expansion to return an equal result:
       * "commandalias q q;commandalias x q;x" should be "x->q->q->quit".
       * P.S.: should also work for "help x" ... */
      if(alias_name != NIL && !su_cs_cmp(word, alias_name))
         flags |= a_NOALIAS;

      if((alias_name = mx_commandalias_exists(word, &alias_exp)) != NIL){
         uz i;

         if(s != NIL){
            s = n_string_push_cp(s, word);
            gecp->gec_hist_cmd = n_string_cp(s);
            s = NIL;
         }

         /* And join arguments onto alias expansion */
         alias_name = word;
         i = strlen(alias_exp);
         cp = line.s;
         line.s = su_AUTO_ALLOC(i + 1 + line.l +1);
         su_mem_copy(line.s, alias_exp, i);
         if(line.l > 0){
            line.s[i++] = ' ';
            su_mem_copy(&line.s[i], cp, line.l);
         }
         line.s[i += line.l] = '\0';
         line.l = i;
         goto jrestart;
      }
   }

   if((cdp = mx_cmd_firstfit(word)) == NIL){
      if(!(flags & a_IS_SKIP) || (n_poption & n_PO_D_V))
         n_err(_("%s: unknown command%s\n"),
            mx_makeprint_cp(word), ((flags & a_IS_SKIP)
               ? _(" (ignored due to `if' condition)") : su_empty));
      gecp->gec_hist_flags = a_GO_HIST_NONE;
      if(flags & a_IS_SKIP)
         goto jret0;
      nerrn = su_ERR_NOSYS;
      goto jleave;
   }

jexec:
   /* The default command is not executed in a macro or when sourcing, when
    * having expanded an alias etc.  To be able to deal with ";reply;~." we
    * need to perform the shell expansion anyway, however */
   if(UNLIKELY(flags & a_IS_EMPTY) &&
         ((n_pstate & n_PS_ROBOT) || !(n_psonce & n_PSO_INTERACTIVE) ||
          alias_name != NIL))
      goto jwhite;

   /* See if we should execute the command -- if a conditional we always
    * execute it, otherwise, check the state of cond.
    * To allow "if 0; echo no; else; echo yes;end" we need to be able to
    * perform input line sequentiation / rest injection even in whiteout
    * situations.  See if we can do that. */
   if(UNLIKELY(flags & a_IS_SKIP) && !(cdp->cd_caflags & mx_CMD_ARG_F)){
jwhite:
      gecp->gec_hist_flags = a_GO_HIST_NONE;

      switch(cdp->cd_caflags & mx_CMD_ARG_TYPE_MASK){
      case mx_CMD_ARG_TYPE_WYRA:{
            char const *v15compat;

            if((v15compat = ok_vlook(v15_compat)) == NIL || *v15compat == '\0')
               break;
         }
         /* FALLTHRU */
      case mx_CMD_ARG_TYPE_MSGLIST:
      case mx_CMD_ARG_TYPE_NDMLIST:
      case mx_CMD_ARG_TYPE_WYSH:
      case mx_CMD_ARG_TYPE_ARG:{
         boole once;

         emsg = line.s;
         for(once = FAL0, s = n_string_creat_auto(&s_b);; once = TRU1){
            u32 shs;

            shs = n_shexp_parse_token((n_SHEXP_PARSE_META_SEMICOLON |
                     n_SHEXP_PARSE_DRYRUN | n_SHEXP_PARSE_TRIM_SPACE |
                     n_SHEXP_PARSE_TRIM_IFSSPACE), s, &line,
                  NIL);

            if(!once && (flags & a_IS_EMPTY) && s->s_len != 0)
               n_err(_("The empty (default) command is ignored here, "
                     "but has arguments: %s\n"), emsg);
            if(line.l == 0)
               break;
            if(shs & n_SHEXP_STATE_META_SEMICOLON){
               ASSERT(shs & n_SHEXP_STATE_STOP);
               mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT, line.s, line.l);
               break;
            }
         }
         }break;
      case mx_CMD_ARG_TYPE_RAWDAT:
      case mx_CMD_ARG_TYPE_STRING:
      case mx_CMD_ARG_TYPE_RAWLIST:
         break;
      }
      goto jret0;
   }

   if(s != NIL && gecp->gec_hist_flags != a_GO_HIST_NONE){
      s = n_string_push_cp(s, cdp->cd_name);
      gecp->gec_hist_cmd = n_string_cp(s);
      /* n_string_gut(n_string_drop_ownership(s)); */
      s = NIL;
   }

   nerrn = su_ERR_INVAL;

   /* Process the arguments to the command, depending on the type it expects */
   UNINIT(emsg, NIL);

   if((cdp->cd_caflags & mx_CMD_ARG_A) && mb.mb_type == MB_VOID){
      emsg = N_("%s: needs an active mailbox\n");
      goto jeflags;
   }
   if((cdp->cd_caflags & mx_CMD_ARG_I) && !(n_psonce & n_PSO_INTERACTIVE) &&
         !(n_poption & n_PO_BATCH_FLAG)){
      emsg = N_("%s: can only be used batch or interactive mode\n");
      goto jeflags;
   }
   if(!(cdp->cd_caflags & mx_CMD_ARG_M) && (n_psonce & n_PSO_SENDMODE)){
      emsg = N_("%s: cannot be used while sending\n");
      goto jeflags;
   }
   if((cdp->cd_caflags & mx_CMD_ARG_NEEDMAC) &&
         ((a_go_ctx->gc_flags & a_GO_TYPE_MACRO_MASK) != a_GO_MACRO)){
      emsg = N_("%s: can only be used in a macro\n");
      goto jeflags;
   }

   if(cdp->cd_caflags & mx_CMD_ARG_R){
      if(n_pstate & n_PS_COMPOSE_MODE){
         /* TODO n_PS_COMPOSE_MODE: should allow `reply': ~:reply! */
         emsg = N_("%s: cannot be used in compose mode\n");
         goto jeflags;
      }
      /* TODO Nothing should prevent mx_CMD_ARG_R in conjunction with
       * TODO n_PS_ROBOT; see a.._may_yield_control()! */
      if((n_pstate & n_PS_ROBOT) && !mx_go_may_yield_control()){
         emsg = N_("%s: cannot be used in this program state\n");
         goto jeflags;
      }
   }

   if((cdp->cd_caflags & mx_CMD_ARG_S) && !(n_psonce & n_PSO_STARTED_CONFIG)){
      emsg = N_("%s: cannot be used during startup\n");
      goto jeflags;
   }
   if(!(cdp->cd_caflags & mx_CMD_ARG_X) && (n_pstate & n_PS_COMPOSE_FORKHOOK)){
      emsg = N_("%s: cannot be used in a hook running in a child process\n");
      goto jeflags;
   }
   if((cdp->cd_caflags & mx_CMD_ARG_W) && !(mb.mb_perm & MB_DELE)){
      emsg = N_("%s: cannot be used in read-only mailbox\n");
jeflags:
      n_err(V_(emsg), cdp->cd_name);
      nerrn = su_ERR_OPNOTSUPP;
      goto jleave;
   }

   if((cdp->cd_caflags & mx_CMD_ARG_O) && /* XXX Remove! -> within command! */
         !su_state_has(su_STATE_REPRODUCIBLE)){
      static struct su_cs_dict a_go__obsol, *a_go_obsol;

      if(UNLIKELY(a_go_obsol == NIL)) /* XXX atexit cleanup */
         a_go_obsol = su_cs_dict_set_treshold_shift(
               su_cs_dict_create(&a_go__obsol, (su_CS_DICT_POW2_SPACED |
                  su_CS_DICT_HEAD_RESORT | su_CS_DICT_ERR_PASS), NIL), 2);

      if(UNLIKELY(!su_cs_dict_has_key(a_go_obsol, cdp->cd_name))){
         su_cs_dict_insert(a_go_obsol, cdp->cd_name, NIL);
         n_err(_("Obsoletion warning: command will be removed: %s\n"),
            cdp->cd_name);
      }
   }

   /* TODO v15: strip n_PS_ARGLIST_MASK off, just in case the actual command
    * TODO does not use any of those list commands which strip this mask,
    * TODO and for now we misuse bits for checking relation to history;
    * TODO argument state should be property of a per-cmd carrier instead */
   n_pstate &= ~n_PS_ARGLIST_MASK;

   if(flags & a_WYSH){
      switch(cdp->cd_caflags & mx_CMD_ARG_TYPE_MASK){
      case mx_CMD_ARG_TYPE_MSGLIST:
      case mx_CMD_ARG_TYPE_NDMLIST:
      case mx_CMD_ARG_TYPE_WYSH:
      case mx_CMD_ARG_TYPE_ARG:
         n_OBSOLETE2(cdp->cd_name, _("`wysh' modifier redundant/needless"));
         flags ^= a_WYSH;
         /* FALLTHRU */
      case mx_CMD_ARG_TYPE_WYRA:
         break;
      case mx_CMD_ARG_TYPE_RAWDAT:
      case mx_CMD_ARG_TYPE_STRING:
      case mx_CMD_ARG_TYPE_RAWLIST:
         n_err(_("%s: wysh: command modifier not supported\n"), cdp->cd_name);
         goto jleave;
      }
   }

   if(flags & a_LOCAL){
      /* `local' is always allowed if `vput' is! */
      if(!(flags & a_VPUT) && !(cdp->cd_caflags & mx_CMD_ARG_L)){
         emsg = N_("%s: command modifier `local' not supported\n");
         goto jeflags; /* above */
      }
      if((a_go_ctx->gc_flags & a_GO_TYPE_MACRO_MASK) != a_GO_MACRO){
         emsg = N_("%s: cannot use `local' modifier in this context\n");
         goto jeflags; /* above */
      }

      flags |= a_WYSH;
      n_pstate |= n_PS_ARGMOD_LOCAL; /* TODO YET useless since stripped later
         * TODO on in getrawlist() etc., i.e., the argument vector producers,
         * TODO therefore yet needs to be set again based on flags&a_LOCAL! */
   }

   if(flags & a_VPUT){
      if(cdp->cd_caflags & mx_CMD_ARG_V){
         emsg = line.s; /* xxx Cannot pass &char* as char const**, so no cp */
         vput = n_shexp_parse_token_cp((n_SHEXP_PARSE_TRIM_SPACE |
               n_SHEXP_PARSE_TRIM_IFSSPACE | n_SHEXP_PARSE_LOG |
               n_SHEXP_PARSE_META_SEMICOLON | n_SHEXP_PARSE_META_KEEP), &emsg);
         line.l -= P2UZ(emsg - line.s);
         line.s = UNCONST(char*,emsg);
         if(emsg == NIL)
            emsg = N_("could not parse input token");
         else if(!n_shexp_is_valid_varname(vput, FAL0))
            emsg = N_("not a valid variable name");
         else if(!n_var_is_user_writable(vput))
            emsg = N_("either not a user writable, or a boolean variable");
         else
            emsg = NIL;
         if(emsg != NIL){
            n_err("%s: %s: vput: %s: %s\n",
               n_ERROR, cdp->cd_name, V_(emsg), n_shexp_quote_cp(vput, FAL0));
            nerrn = su_ERR_NOTSUP;
            rv = -1;
            goto jleave;
         }
         n_pstate |= n_PS_ARGMOD_VPUT; /* TODO YET useless since stripped later
         * TODO on in getrawlist() etc., i.e., the argument vector producers,
         * TODO therefore yet needs to be set again based on flags&a_VPUT! */
      }else{
         n_err(_("%s: %s: vput: command modifier not supported\n"),
            n_ERROR, cdp->cd_name);
         mx_cmd_print_synopsis(cdp, NIL);
         flags &= ~a_VPUT;
      }
   }

   if(n_poption & n_PO_D_VV)
      n_err(_("COMMAND <%s> %s\n"), cdp->cd_name, line.s);

   switch(cdp->cd_caflags & mx_CMD_ARG_TYPE_MASK){
   case mx_CMD_ARG_TYPE_MSGLIST:
      /* Message list defaulting to nearest forward legal message */
      if(n_msgvec == NIL)
         goto jmsglist_err;
      if((c = n_getmsglist(line.s, n_msgvec, cdp->cd_mflags_o_minargs, NIL)
            ) < 0){
         nerrn = su_ERR_NOMSG;
         flags |= a_NO_ERRNO;
         break;
      }
      if(c == 0){
         if((n_msgvec[0] = first(cdp->cd_mflags_o_minargs,
               cdp->cd_mmask_o_maxargs)) != 0){
            c = 1;
            n_msgmark1 = &message[n_msgvec[0] - 1];
         }else{
jmsglist_err:
            if(!(n_pstate & (n_PS_HOOK_MASK | n_PS_ROBOT)) ||
                  (n_poption & n_PO_D_V))
               n_err(_("No applicable messages\n"));
            nerrn = su_ERR_NOMSG;
            /* flags |= a_NO_ERRNO;*/
            break;
         }
      }
jmsglist_go:
      /* C99 */{
         int *mvp;

         mvp = su_AUTO_CALLOC_N(sizeof *mvp, c +1);
         while(c-- > 0)
            mvp[c] = n_msgvec[c];
         if(!(flags & a_NO_ERRNO) && !(cdp->cd_caflags & mx_CMD_ARG_EM))/*XXX*/
            su_err_set_no(su_ERR_NONE);
         rv = (*cdp->cd_func)(mvp);
      }
      break;

   case mx_CMD_ARG_TYPE_NDMLIST:
      /* Message list with no defaults, but no error if none exist */
      if(n_msgvec == NIL)
         goto jmsglist_err;
      if((c = n_getmsglist(line.s, n_msgvec, cdp->cd_mflags_o_minargs, NIL)
            ) < 0){
         nerrn = su_ERR_NOMSG;
         flags |= a_NO_ERRNO;
         break;
      }
      goto jmsglist_go;

   case mx_CMD_ARG_TYPE_STRING:
      /* Just the straight string, old style, with leading blanks removed */
      for(cp = line.s; su_cs_is_space(*cp);)
         ++cp;
      if(!(flags & a_NO_ERRNO) && !(cdp->cd_caflags & mx_CMD_ARG_EM)) /* XXX */
         su_err_set_no(su_ERR_NONE);
      rv = (*cdp->cd_func)(cp);
      break;

   case mx_CMD_ARG_TYPE_RAWDAT:
      /* Just the straight string, placed in argv[] */
      argvp = argv_stack;
      if(flags & a_VPUT)
         *argvp++ = vput;
      *argvp++ = line.s;
      *argvp = NIL;
      if(!(flags & a_NO_ERRNO) && !(cdp->cd_caflags & mx_CMD_ARG_EM)) /* XXX */
         su_err_set_no(su_ERR_NONE);
      rv = (*cdp->cd_func)(argv_stack);
      break;

   case mx_CMD_ARG_TYPE_WYSH:
      c = 1;
      if(0){
         /* FALLTHRU */
   case mx_CMD_ARG_TYPE_WYRA:
         /* C99 */{
            char const *v15compat;

            if((v15compat = ok_vlook(v15_compat)) != NIL && *v15compat != '\0')
               flags |= a_WYSH;
         }
         c = (flags & a_WYSH) ? 1 : 0;
         if(0){
   case mx_CMD_ARG_TYPE_RAWLIST:
            c = 0;
         }
      }

      argvp = argv_base = su_AUTO_ALLOC(sizeof(*argv_base) * n_MAXARGC);
      if(flags & a_VPUT)
         *argvp++ = vput;
      if((c = getrawlist((c != 0), argvp,
            (n_MAXARGC - ((flags & a_VPUT) != 0)), line.s, line.l)) < 0){
         n_err(_("%s: invalid argument list\n"), cdp->cd_name);
         flags |= a_NO_ERRNO;
         break;
      }

      if(UCMP(32, c, <, cdp->cd_mflags_o_minargs) ||
            UCMP(32, c, >, cdp->cd_mmask_o_maxargs)){
         n_err(_("%s: %s: takes at least %u, and no more than %u arg(s)\n"),
            n_ERROR, cdp->cd_name, S(u32,cdp->cd_mflags_o_minargs),
            S(u32,cdp->cd_mmask_o_maxargs));
         mx_cmd_print_synopsis(cdp, NIL);
         flags |= a_NO_ERRNO;
         break;
      }

      if(flags & a_WYSH)
         n_pstate |= n_PS_ARGMOD_WYSH;
      if(flags & a_LOCAL)
         n_pstate |= n_PS_ARGMOD_LOCAL;
      if(flags & a_VPUT)
         n_pstate |= n_PS_ARGMOD_VPUT; /* TODO due to getrawlist(), as above */

      if(!(flags & a_NO_ERRNO) && !(cdp->cd_caflags & mx_CMD_ARG_EM)) /* XXX */
         su_err_set_no(su_ERR_NONE);
      rv = (*cdp->cd_func)(argv_base);
      if(a_go_xcall != NIL)
         goto jret0;
      break;

   case mx_CMD_ARG_TYPE_ARG:{
      /* TODO The _ARG_TYPE_ARG is preliminary, in the end we should have a
       * TODO per command-ctx carrier that also has slots for it arguments,
       * TODO and that should be passed along all the way.  No more arglists
       * TODO here, etc. */
      struct mx_cmd_arg_ctx cac;

      cac.cac_desc = cdp->cd_cadp;
      cac.cac_indat = line.s;
      cac.cac_inlen = line.l;
      cac.cac_msgflag = cdp->cd_mflags_o_minargs;
      cac.cac_msgmask = cdp->cd_mmask_o_maxargs;
      if(!mx_cmd_arg_parse(&cac)){
         flags |= a_NO_ERRNO;
         break;
      }

      if(flags & a_VPUT){
         cac.cac_vput = vput;
         /* Global "hack" not used: n_pstate |= n_PS_ARGMOD_VPUT; */
         if(flags & a_LOCAL){
            cac.cac_cm_local = TRU1;
            /* Global "hack" not used: n_pstate |= n_PS_ARGMOD_LOCAL; */
         }
      }else{
         ASSERT(cac.cac_cm_local == FAL0);
         ASSERT(cac.cac_vput == NIL);
      }

      if(!(flags & a_NO_ERRNO) && !(cdp->cd_caflags & mx_CMD_ARG_EM)) /* XXX */
         su_err_set_no(su_ERR_NONE);
      rv = (*cdp->cd_func)(&cac);
      if(a_go_xcall != NIL)
         goto jret0;
      }break;

   default:
      DBG( n_panic(_("Implementation error: unknown argument type: %d"),
         cdp->cd_caflags & mx_CMD_ARG_TYPE_MASK); )
      nerrn = su_ERR_NOTOBACCO;
      nexn = 1;
      goto jret0;
   }

   if(gecp->gec_hist_flags & a_GO_HIST_ADD){
      if(cdp->cd_caflags & mx_CMD_ARG_NOHIST)
         gecp->gec_hist_flags = a_GO_HIST_NONE;
      else if((cdp->cd_caflags & mx_CMD_ARG_HGABBY) ||
            (n_pstate & n_PS_MSGLIST_GABBY))
         gecp->gec_hist_flags |= a_GO_HIST_GABBY;
   }

   if(rv != 0){
      if(!(flags & a_NO_ERRNO)){
         if(cdp->cd_caflags & mx_CMD_ARG_EM)
            flags |= a_NO_ERRNO;
         else if((nerrn = su_err_no()) == 0)
            nerrn = su_ERR_INVAL;
      }/*else
         flags ^= a_NO_ERRNO;*/
   }else if(cdp->cd_caflags & mx_CMD_ARG_EM)
      flags |= a_NO_ERRNO;
   else
      nerrn = su_ERR_NONE;

jleave:
   if((nexn = rv) != 0 &&
         (gecp->gec_hist_flags & (a_GO_HIST_ADD | a_GO_HIST_INIT)
            ) == (a_GO_HIST_ADD | a_GO_HIST_INIT))
      gecp->gec_hist_flags |= a_GO_HIST_GABBY_ERROR;

   if(flags & a_IGNERR){
      /* Take care not to overwrite existing exit status */
      if(!(n_psonce & n_PSO_EXIT_MASK) && !(n_pstate & n_PS_ERR_EXIT_MASK))
         n_exit_status = n_EXIT_OK;
      n_pstate &= ~n_PS_ERR_EXIT_MASK;
   }else if(rv != 0){
      if(ok_blook(errexit))
         n_pstate |= n_PS_ERR_QUIT;
      else if(ok_blook(posix)){
         if(n_psonce & n_PSO_STARTED)
            rv = 0;
         else if(!(n_psonce & n_PSO_INTERACTIVE))
            n_pstate |= n_PS_ERR_XIT;
      }else
         rv = 0;

      if(rv != 0){
         if(n_exit_status == n_EXIT_OK)
            n_exit_status = n_EXIT_ERR;
         if((n_poption & n_PO_D_V) &&
               !(n_psonce & (n_PSO_INTERACTIVE | n_PSO_STARTED)))
            n_alert(_("Non-interactive, bailing out due to errors "
               "in startup load phase"));
         goto jret;
      }
   }

   if(cdp == NIL)
      goto jret0;
   if((cdp->cd_caflags & mx_CMD_ARG_P) && ok_blook(autoprint) && visible(dot))
      mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT, "\\type",
         sizeof("\\type") -1);

   if(!(cdp->cd_caflags & mx_CMD_ARG_T) && !(gcp->gc_flags & a_GO_FILE) &&
         !(n_pstate & n_PS_HOOK_MASK))
      n_pstate |= n_PS_SAW_COMMAND;

jret0:
   rv = 0;
jret:
   if(!(flags & a_NO_ERRNO))
      n_pstate_err_no = nerrn;
   n_pstate_ex_no = nexn;

   NYD_OU;
   return (rv == 0);
}

static void
a_go_hangup(int s){
   NYD; /* Signal handler */
   UNUSED(s);
   /* nothing to do? */
   exit(n_EXIT_ERR);
}

#ifdef mx_HAVE_IMAP
EXPORT void mx_go_onintr_for_imap(void);
void mx_go_onintr_for_imap(void){a_go_onintr(0);}
#endif

static void
a_go_onintr(int s){ /* TODO block signals while acting */
   NYD; /* Signal handler */
   UNUSED(s);

   safe_signal(SIGINT, a_go_onintr);

   mx_termios_cmdx(mx_TERMIOS_CMD_RESET);

   /* TODO Each event_loop yet has its own INT layer, therefore when we come
    * TODO here to UNWIND we are, effectively, already at outermost layer :( */
   ASSERT(a_go_ctx->gc_outer == NIL);
   a_go_cleanup(a_go_ctx,
      a_GO_CLEANUP_UNWIND | /* XXX FAKE */a_GO_CLEANUP_HOLDALLSIGS);

   if(interrupts != 1)
      n_err_sighdl(_("Interrupt\n"));
   safe_signal(SIGPIPE, a_go_oldpipe);
   siglongjmp(a_go_srbuf, 0); /* FIXME get rid */
}

static void
a_go_cleanup(struct a_go_ctx *gcp, BITENUM_IS(u32,a_go_cleanup_mode) gcm){
   /* Signals blocked */
   NYD_IN;

   if(!(gcm & a_GO_CLEANUP_HOLDALLSIGS))
      mx_sigs_all_holdx();

jrestart:
   /* Free input injections of this level first */
   if(!(gcm & a_GO_CLEANUP_LOOPTICK)){
      struct a_go_input_inject **giipp, *giip;

      for(giipp = &gcp->gc_inject; (giip = *giipp) != NIL;){
         *giipp = giip->gii_next;
         su_FREE(giip);
      }
   }

   /* Cleanup non-crucial external stuff */
   mx_COLOUR(
      if(gcp->gc_data.gdc_colour != NIL)
         mx_colour_stack_del(&gcp->gc_data);
   )

   /* Cleanup crucial external stuff as necessary */
   if(gcp->gc_data.gdc_ifcond != NIL &&
         ((gcp->gc_outer == NIL && (gcm & a_GO_CLEANUP_UNWIND)) ||
            !(gcm & a_GO_CLEANUP_LOOPTICK))){
      mx_cnd_if_stack_del(&gcp->gc_data);

      if(!(gcm & (a_GO_CLEANUP_ERROR | a_GO_CLEANUP_SIGINT)) &&
            !(gcp->gc_flags & a_GO_FORCE_EOF) &&
            !(n_psonce & n_PSO_EXIT_MASK)){
         n_err(_("Unmatched `if' at end of %s%s\n"),
            (gcp->gc_outer == NIL ? su_empty
             : ((gcp->gc_flags & a_GO_MACRO
              ? (gcp->gc_flags & a_GO_MACRO_CMD ? _("command ") : _("macro "))
              : _("`source'd or resource file")))),
            gcp->gc_name);
         gcm |= a_GO_CLEANUP_ERROR;
      }
   }

   /* Work the actual context (according to cleanup mode) */
   if(gcp->gc_outer == NIL){
      ASSERT(!(gcp->gc_flags & a_GO_TYPE_MASK));
      if(gcm & (a_GO_CLEANUP_UNWIND | a_GO_CLEANUP_SIGINT)){
         if(a_go_xcall != NIL){
            ASSERT(a_go_xcall != R(void*,-1));
            su_FREE(a_go_xcall);
            a_go_xcall = NIL;
         }
         gcp->gc_flags &= ~a_GO_XCALL_LOOP_MASK;
         n_pstate &= ~n_PS_ERR_EXIT_MASK;
      }

      mx_fs_close_all();

      su_mem_bag_reset(gcp->gc_data.gdc_membag);
      DBG( su_mem_set_conf(su_MEM_CONF_LINGER_FREE_RELEASE, 0); )

      n_pstate &= ~n_PS_ROBOT;
      ASSERT(a_go_xcall == NIL);
      ASSERT(!(gcp->gc_flags & a_GO_XCALL_LOOP_MASK));
      ASSERT(gcp->gc_on_finalize == NIL);
      mx_COLOUR( ASSERT(gcp->gc_data.gdc_colour == NIL); )

      if(gcm & a_GO_CLEANUP_ERROR)
         goto jerr;
      goto jxleave;
   }else if(gcm & a_GO_CLEANUP_LOOPTICK){
      su_mem_bag_reset(gcp->gc_data.gdc_membag);
      DBG( su_mem_set_conf(su_MEM_CONF_LINGER_FREE_RELEASE, 0); )
      goto jxleave;
   }else if(gcp->gc_flags & a_GO_SPLICE){ /* TODO Temporary hack */
      n_stdin = gcp->gc_splice_stdin;
      n_stdout = gcp->gc_splice_stdout;
      n_psonce = gcp->gc_splice_psonce;
      goto jstackpop;
   }

   /* Teardown context */
   if(gcp->gc_flags & a_GO_MACRO){
      if(gcp->gc_flags & a_GO_MACRO_FREE_DATA){
         char **lp;

         while(*(lp = &gcp->gc_lines[gcp->gc_loff]) != NIL){
            su_FREE(*lp);
            ++gcp->gc_loff;
         }
         /* Part of gcp's memory chunk, then */
         if(!(gcp->gc_flags & a_GO_MACRO_CMD))
            su_FREE(gcp->gc_lines);
      }
   }else if(gcp->gc_flags & a_GO_PIPE)
      /* XXX command manager should -TERM then -KILL instead of hoping
       * XXX for exit of provider due to su_ERR_PIPE / SIGPIPE */
      mx_fs_pipe_close(gcp->gc_file, TRU1);
   else if(gcp->gc_flags & a_GO_FILE)
      mx_fs_close(gcp->gc_file);

   if(!(gcp->gc_flags & a_GO_MEMBAG_INHERITED))
      su_mem_bag_gut(gcp->gc_data.gdc_membag);
   else
      su_mem_bag_reset(gcp->gc_data.gdc_membag);

jstackpop:
   /* xxx Update a_go_ctx and go_data, n_pstate ... */
   a_go_ctx = gcp->gc_outer;
   ASSERT(a_go_ctx != NIL);
   /* C99 */{
      struct a_go_ctx *x;

      for(x = a_go_ctx; x->gc_flags & a_GO_DATACTX_INHERITED;){
         x = x->gc_outer;
         ASSERT(x != NIL);
      }
      mx_go_data = &x->gc_data;
   }

   if(!(a_go_ctx->gc_flags & a_GO_TYPE_MASK))
      n_pstate &= ~n_PS_ROBOT;
   else
      ASSERT(n_pstate & n_PS_ROBOT);

   if(gcp->gc_on_finalize != NIL)
      (*gcp->gc_on_finalize)(gcp->gc_finalize_arg);

   if(gcm & a_GO_CLEANUP_ERROR){
      if(a_go_ctx->gc_flags & a_GO_XCALL_LOOP)
         a_go_ctx->gc_flags |= a_GO_XCALL_LOOP_ERROR;
      goto jerr;
   }

jleave:
   if(gcp->gc_flags & a_GO_FREE)
      su_FREE(gcp);

   if(UNLIKELY(gcm & a_GO_CLEANUP_UNWIND) && UNLIKELY(gcp != a_go_ctx)){
      /* TODO GO_CLEANUP_UNWIND is a fake yet: gcp should be NIL on entry then,
       * TODO and we should do the right thing, then (see notes around) */
      gcp = a_go_ctx;
      goto jrestart;
   }

jxleave:
   NYD_OU;
   if(!(gcm & a_GO_CLEANUP_HOLDALLSIGS))
      mx_sigs_all_rele();
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
          (gcp->gc_outer == NIL ||
           !(gcp->gc_outer->gc_flags & a_GO_TYPE_MASK))))
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
                     ? _("loading `source'd file") : _(a_GO_MAINCTX_NAME)))))
          : (((gcp->gc_flags & (a_GO_MACRO | a_GO_MACRO_BLTIN_RC)
               ) == a_GO_MACRO)
             ? ((gcp->gc_flags & a_GO_MACRO_X_OPTION)
                ? _("evaluating command line")
                : _("evaluating macro"))
             : _("loading initialization resource"))),
         n_shexp_quote_cp(gcp->gc_name, FAL0),
         (n_poption & n_PO_D ? su_empty : _(" (enable *debug* for trace)")));
   goto jleave;
}

static boole
a_go_file(char const *file, boole silent_open_error){
   struct a_go_ctx *gcp;
   sigset_t osigmask;
   uz nlen;
   char *nbuf;
   boole ispipe;
   FILE *fip;
   NYD_IN;

   fip = NIL;
   UNINIT(nbuf, NIL);

   /* Being a command argument file is space-trimmed *//* TODO v15 with
    * TODO WYRALIST this is no longer necessary true, and for that we
    * TODO don't set _PARSE_TRIM_SPACE because we cannot! -> cmd.h!! */
#if 0
   ((ispipe = (!silent_open_error && (nlen = su_cs_len(file)) > 0 &&
         file[--nlen] == '|')))
#else
   ispipe = FAL0;
   if(!silent_open_error){
      for(nlen = su_cs_len(file); nlen > 0;){
         char c;

         c = file[--nlen];
         if(!su_cs_is_space(c)){
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
      if((fip = mx_fs_pipe_open(nbuf /* #if 0 above = savestrbuf(file, nlen)*/,
            "r", ok_vlook(SHELL), NIL, -1)) == NIL)
         goto jeopencheck;
   }else if((nbuf = fexpand(file, FEXP_LOCAL_FILE | FEXP_NVAR)) == NIL)
      goto jeopencheck;
   else if((fip = mx_fs_open(nbuf, "r")) == NIL){
jeopencheck:
      if(!silent_open_error || (n_poption & n_PO_D_V))
         n_perr(nbuf, 0);
      if(silent_open_error)
         fip = R(FILE*,-1);
      goto jleave;
   }

   sigprocmask(SIG_BLOCK, NIL, &osigmask);

   gcp = su_ALLOC(VSTRUCT_SIZEOF(struct a_go_ctx,gc_name) +
         (nlen = su_cs_len(nbuf) +1));
   su_mem_set(gcp, 0, VSTRUCT_SIZEOF(struct a_go_ctx,gc_name));
   gcp->gc_data.gdc_membag =
         su_mem_bag_create(&gcp->gc_data.gdc__membag_buf[0], 0);

   mx_sigs_all_holdx();

   gcp->gc_outer = a_go_ctx;
   gcp->gc_osigmask = osigmask;
   gcp->gc_file = fip;
   gcp->gc_flags = (ispipe ? a_GO_FREE | a_GO_PIPE : a_GO_FREE | a_GO_FILE) |
         (a_go_ctx->gc_flags & a_GO_MACRO_ROBOT ? a_GO_MACRO_ROBOT : 0) |
         a_GO_FILE_ROBOT;
   su_mem_copy(gcp->gc_name, nbuf, nlen);

   a_go_ctx = gcp;
   mx_go_data = &gcp->gc_data;
   n_pstate |= n_PS_ROBOT;
   if(!a_go_event_loop(gcp, mx_GO_INPUT_NONE | mx_GO_INPUT_NL_ESC))
      fip = NIL;

jleave:
   NYD_OU;
   return (fip != NIL);
}

static boole
a_go_load(struct a_go_ctx *gcp){
   NYD2_IN;

   ASSERT(!(n_psonce & n_PSO_STARTED));
   ASSERT(!(a_go_ctx->gc_flags & a_GO_TYPE_MASK));

   gcp->gc_flags |= a_GO_MEMBAG_INHERITED;
   gcp->gc_data.gdc_membag = mx_go_data->gdc_membag;

   mx_sigs_all_holdx();

   /* POSIX:
    *    Any errors in the start-up file shall either cause mailx to terminate
    *    with a diagnostic message and a non-zero status or to continue after
    *    writing a diagnostic message, ignoring the remainder of the lines in
    *    the start-up file. */
   gcp->gc_outer = a_go_ctx;
   a_go_ctx = gcp;
   mx_go_data = &gcp->gc_data;

   n_pstate |= n_PS_ROBOT;

   mx_sigs_all_rele();

   mx_go_main_loop(FAL0);

   NYD2_OU;
   return (((n_psonce & n_PSO_EXIT_MASK) |
      (n_pstate & n_PS_ERR_EXIT_MASK)) == 0);
}

static void
a_go__eloopint(int sig){ /* TODO one day, we don't need it no more */
   NYD; /* Signal handler */
   UNUSED(sig);
   siglongjmp(a_go_ctx->gc_eloop_jmp, 1);
}

static boole
a_go_event_loop(struct a_go_ctx *gcp, BITENUM_IS(u32,mx_go_input_flags) gif){
   n_sighdl_t soldhdl;
   struct a_go_eval_ctx gec;
   enum {a_RETOK = TRU1, a_TICKED = 1<<1} volatile f;
   volatile int hadint;/* TODO get rid of shitty signal stuff (see signal.c) */
   sigset_t osigmask;
   NYD2_IN;

   su_mem_set(&gec, 0, sizeof gec);
   if(gif & mx_GO_INPUT_IGNERR)
      gec.gec_ignerr = TRU1;
   mx_fs_linepool_aquire(&gec.gec_line.s, &gec.gec_line.l);

   osigmask = gcp->gc_osigmask;
   hadint = FAL0;
   f = a_RETOK;

   if((soldhdl = safe_signal(SIGINT, SIG_IGN)) != SIG_IGN){
      safe_signal(SIGINT, &a_go__eloopint);
      if(sigsetjmp(gcp->gc_eloop_jmp, 1)){
         mx_sigs_all_holdx();
         hadint = TRU1;
         f &= ~a_RETOK;
         gcp->gc_flags &= ~a_GO_XCALL_LOOP_MASK;
         goto jjump;
      }
   }

   for(;; f |= a_TICKED){
      int n;

      if(f & a_TICKED){
         su_mem_bag_reset(gcp->gc_data.gdc_membag);
         DBG( su_mem_set_conf(su_MEM_CONF_LINGER_FREE_RELEASE, 0); )
      }

      /* Read a line of commands and handle end of file specially */
      gec.gec_line.l = gec.gec_line_size;
      mx_sigs_all_rele();
      n = mx_go_input(gif, NIL, &gec.gec_line.s, &gec.gec_line.l, NIL, NIL);
      mx_sigs_all_holdx();
      gec.gec_line_size = S(u32,gec.gec_line.l);
      gec.gec_line.l = S(u32,n);

      if(n < 0)
         break;

      mx_sigs_all_rele();
      ASSERT(gec.gec_hist_flags == a_GO_HIST_NONE);
      if(!a_go_evaluate(gcp, &gec))
         f &= ~a_RETOK;
      mx_sigs_all_holdx();

      if(!(f & a_RETOK) || a_go_xcall != NIL ||
            (n_psonce & n_PSO_EXIT_MASK) || (n_pstate & n_PS_ERR_EXIT_MASK))
         break;
   }

jjump: /* TODO Should be _CLEANUP_UNWIND not _TEARDOWN on signal if DOABLE!
   * TODO We would need a way to drop all the stack in one go, however,
   * TODO and yet assert condition that UNWIND is only set for outermost */
   a_go_cleanup(gcp, a_GO_CLEANUP_TEARDOWN |
      (f & a_RETOK ? 0 : a_GO_CLEANUP_ERROR) |
      (hadint ? a_GO_CLEANUP_SIGINT : 0) | a_GO_CLEANUP_HOLDALLSIGS);

   mx_fs_linepool_release(gec.gec_line.s, gec.gec_line_size);

   if(soldhdl != SIG_IGN)
      safe_signal(SIGINT, soldhdl);

   NYD2_OU;
   mx_sigs_all_rele();
   if(hadint){
      sigprocmask(SIG_SETMASK, &osigmask, NIL);
      n_raise(SIGINT);
   }
   return (f & a_RETOK);
}

void
mx_go_init(void){
   struct a_go_ctx *gcp;
   NYD2_IN;

   ASSERT(n_stdin != NIL);

   gcp = S(void*,a_go__mainctx_b.uf);
   DBGOR( su_mem_set(gcp, 0, VSTRUCT_SIZEOF(struct a_go_ctx,gc_name)),
      su_mem_set(&gcp->gc_data, 0, sizeof gcp->gc_data) );
   gcp->gc_data.gdc_membag =
         su_mem_bag_create(&gcp->gc_data.gdc__membag_buf[0], 0);
   gcp->gc_file = n_stdin;
   su_mem_copy(gcp->gc_name, a_GO_MAINCTX_NAME, sizeof(a_GO_MAINCTX_NAME));

   a_go_ctx = gcp;
   mx_go_data = &gcp->gc_data;

   mx_termios_controller_setup(mx_TERMIOS_SETUP_STARTUP);
   mx_child_controller_setup();

   NYD2_OU;
}

boole
mx_go_main_loop(boole main_call){ /* FIXME */
   struct a_go_eval_ctx gec;
   int n, eofcnt;
   boole volatile rv;
   NYD_IN;

   rv = TRU1;

   if(main_call){
      if(safe_signal(SIGINT, SIG_IGN) != SIG_IGN)
         safe_signal(SIGINT, &a_go_onintr);
      if(safe_signal(SIGHUP, SIG_IGN) != SIG_IGN)
         safe_signal(SIGHUP, &a_go_hangup);
   }
   a_go_oldpipe = safe_signal(SIGPIPE, SIG_IGN);
   safe_signal(SIGPIPE, a_go_oldpipe);

   su_mem_set(&gec, 0, sizeof gec);

   (void)sigsetjmp(a_go_srbuf, 1); /* FIXME get rid */
   mx_sigs_all_holdx();

   for(eofcnt = 0;; gec.gec_ever_seen = TRU1){
      interrupts = 0;
      DVL( su_nyd_reset_level(1); )

      if(gec.gec_ever_seen)
         /* TODO too expensive, just do the membag (++?) here.
          * TODO in fact all other conditions would be an error, no? */
         a_go_cleanup(a_go_ctx,
            a_GO_CLEANUP_LOOPTICK | a_GO_CLEANUP_HOLDALLSIGS);

      /* TODO This condition test may not be here: if the condition is not true
       * TODO a recursive mainloop object without that cruft should be used! */
      if(!(n_pstate & n_PS_ROBOT)){
         if(a_go_ctx->gc_inject == NIL)
            mx_fs_linepool_cleanup(FAL0);

         /* TODO We need a regular on_tick_event, to which this one, the
          * TODO *newmail* thing below, and possibly other caches
          * TODO (mime.types, mta-aliases, mailcap, netrc; if not yet:
          * TODO convert!!) can attach: they should trigger a switch and
          * TODO update cache state only once per mainloop tick!! */
         /* C99 */{
            char const *ccp;

            if((ccp = ok_vlook(on_main_loop_tick)) != NIL)
               temporary_on_xy_hook_caller("on-main-loop-tick", ccp, TRU1);
         }

         /* Do not check newmail with active injections, wait for prompt */
         if(a_go_ctx->gc_inject == NIL && (n_psonce & n_PSO_INTERACTIVE)){
            char *cp;

            if((cp = ok_vlook(newmail)) != NIL){ /* TODO -> on_tick_event! */
               struct stat st;

               if(mb.mb_type == MB_FILE){
                  if(!stat(mailname, &st) && st.st_size > mailsize)
#if defined mx_HAVE_MAILDIR || defined mx_HAVE_IMAP
                  Jnewmail:
#endif
                  {
                     u32 odid;
                     uz odot;

                     odot = P2UZ(dot - message);
                     odid = (n_pstate & n_PS_DID_PRINT_DOT);

                     mx_sigs_all_rele();
                     n = setfile(mailname,
                           (FEDIT_NEWMAIL |
                              ((mb.mb_perm & MB_DELE) ? 0 : FEDIT_RDONLY)));
                     mx_sigs_all_holdx();

                     if(n < 0) {
                        n_exit_status |= n_EXIT_ERR;
                        rv = FAL0;
                        break;
                     }
#ifdef mx_HAVE_IMAP
                     if(mb.mb_type != MB_IMAP){
#endif
                        dot = &message[odot];
                        n_pstate |= odid;
#ifdef mx_HAVE_IMAP
                     }
#endif
                  }
               }else{
#if defined mx_HAVE_MAILDIR || defined mx_HAVE_IMAP
                  n = (cp != NIL && su_cs_cmp(cp, "nopoll"));
#endif

#ifdef mx_HAVE_MAILDIR
                  if(mb.mb_type == MB_MAILDIR){
                     if(n != 0)
                        goto Jnewmail;
                  }
#endif
#ifdef mx_HAVE_IMAP
                  if(mb.mb_type == MB_IMAP){
                     if(!n)
                        n = (cp != NIL && su_cs_cmp(cp, "noimap"));

                     if(imap_newmail(n) > (cp == NIL))
                        goto Jnewmail;
                  }
#endif
               }
            }
         }
      }

      /* Read a line of commands and handle end of file specially */
      n_pstate |= n_PS_ERRORS_NEED_PRINT_ONCE;

      mx_fs_linepool_aquire(&gec.gec_line.s, &gec.gec_line.l);
      gec.gec_line_size = S(u32,gec.gec_line.l);
      /* C99 */{
         boole histadd;

         histadd = ((n_psonce & n_PSO_INTERACTIVE) &&
               !(n_pstate & n_PS_ROBOT) &&
                a_go_ctx->gc_inject == NIL); /* xxx really injection? */
         mx_sigs_all_rele();
         ASSERT(!gec.gec_ignerr);
         n = mx_go_input(mx_GO_INPUT_CTX_DEFAULT | mx_GO_INPUT_NL_ESC, NIL,
               &gec.gec_line.s, &gec.gec_line.l, NIL, &histadd);
         mx_sigs_all_holdx();

         gec.gec_hist_flags = histadd ? a_GO_HIST_ADD : a_GO_HIST_NONE;
      }
      gec.gec_line_size = S(u32,gec.gec_line.l);
      gec.gec_line.l = S(u32,n);

      if(n < 0){
         mx_fs_linepool_release(gec.gec_line.s, gec.gec_line_size);
         if(!(n_pstate & n_PS_ROBOT) &&
               (n_psonce & n_PSO_INTERACTIVE) && ok_blook(ignoreeof) &&
               ++eofcnt < 4){
            fprintf(n_stdout, _("*ignoreeof* set, use `quit' to quit.\n"));
            mx_go_input_clearerr();
            continue;
         }
         break;
      }

      n_pstate &= ~n_PS_HOOK_MASK;
      mx_sigs_all_rele();
      rv = a_go_evaluate(a_go_ctx, &gec);
      mx_sigs_all_holdx();

      n_pstate &= ~n_PS_ERRORS_NEED_PRINT_ONCE;
      switch(n_pstate & n_PS_ERR_EXIT_MASK){
      case n_PS_ERR_XIT: n_psonce |= n_PSO_XIT; break;
      case n_PS_ERR_QUIT: n_psonce |= n_PSO_QUIT; break;
      default: break;
      }

      if(gec.gec_hist_flags & a_GO_HIST_ADD){
         char const *cc, *ca;

         /* TODO history handling is terrible; this should pass the command
          * TODO evaluation context carrier all along the way, so that commands
          * TODO can alter the "add history" behaviour at will; also the
          * TODO arguments as passed into ARGV should be passed along to
          * TODO addhist, see *on-history-addition* for the why of this */
         cc = gec.gec_hist_cmd;
         ca = gec.gec_hist_args;
         if(cc != NIL && ca != NIL)
            cc = savecatsep(cc, ' ', ca);
         else if(ca != NIL)
            cc = ca;
         ASSERT(cc != NIL);
         mx_tty_addhist(cc, (mx_GO_INPUT_CTX_DEFAULT |
            (gec.gec_hist_flags & a_GO_HIST_GABBY
               ? mx_GO_INPUT_HIST_GABBY : mx_GO_INPUT_NONE) |
            (gec.gec_hist_flags & a_GO_HIST_GABBY_ERROR
               ?  mx_GO_INPUT_HIST_GABBY | mx_GO_INPUT_HIST_ERROR
               : mx_GO_INPUT_NONE)));
      }

      mx_fs_linepool_release(gec.gec_line.s, gec.gec_line_size);

      if((n_psonce & n_PSO_EXIT_MASK) || !rv)
         break;
   }

   a_go_cleanup(a_go_ctx, (a_GO_CLEANUP_TEARDOWN | a_GO_CLEANUP_HOLDALLSIGS |
      (rv ? 0 : a_GO_CLEANUP_ERROR)));
   mx_fs_linepool_cleanup(TRU1);

   mx_sigs_all_rele();

   NYD_OU;
   return rv;
}

void
mx_go_input_clearerr(void){
   FILE *fp;
   NYD2_IN;

   fp = NIL;

   if(!(a_go_ctx->gc_flags & (a_GO_FORCE_EOF |
         a_GO_PIPE | a_GO_MACRO | a_GO_SPLICE)))
      fp = a_go_ctx->gc_file;

   if(fp != NIL){
      a_go_ctx->gc_flags &= ~a_GO_IS_EOF;
      clearerr(fp);
   }

   NYD2_OU;
}

void
mx_go_input_force_eof(void){ /* xxx inline */
   NYD2_IN;

   a_go_ctx->gc_flags |= a_GO_FORCE_EOF;

   NYD2_OU;
}

boole
mx_go_input_is_eof(void){ /* xxx inline */
   boole rv;
   NYD2_IN;

   rv = ((a_go_ctx->gc_flags & a_GO_IS_EOF) != 0);

   NYD2_OU;
   return rv;
}

boole
mx_go_input_have_injections(void){ /* xxx inline */
   boole rv;
   NYD2_IN;

   rv = (a_go_ctx->gc_inject != NIL);

   NYD2_OU;
   return rv;
}

void
mx_go_input_inject(BITENUM_IS(u32,mx_go_input_inject_flags) giif,
      char const *buf, uz len){
   NYD_IN;

   if(len == UZ_MAX)
      len = su_cs_len(buf);

   if(UZ_MAX - VSTRUCT_SIZEOF(struct a_go_input_inject,gii_dat) -1 > len &&
         len > 0){
      struct a_go_input_inject *giip, **giipp;

      mx_sigs_all_holdx();

      giip = su_ALLOC(VSTRUCT_SIZEOF(struct a_go_input_inject,gii_dat) +
            1 + len +1);
      giipp = &a_go_ctx->gc_inject;
      giip->gii_next = *giipp;
      giip->gii_commit = ((giif & mx_GO_INPUT_INJECT_COMMIT) != 0);
      giip->gii_no_history = ((giif & mx_GO_INPUT_INJECT_HISTORY) == 0);
      su_mem_copy(&giip->gii_dat[0], buf, len);
      giip->gii_dat[giip->gii_len = len] = '\0';
      *giipp = giip;

      mx_sigs_all_rele();
   }

   NYD_OU;
}

int
(mx_go_input)(BITENUM_IS(u32,mx_go_input_flags) gif, char const *prompt,
      char **linebuf, uz *linesize, char const *string, boole *histok_or_nil
      su_DBG_LOC_ARGS_DECL){
   /* TODO readline: linebuf pool!; mx_go_input should return s64.
    * TODO This thing should be replaced by a(n) (stack of) event generator(s)
    * TODO and consumed by OnLineCompletedEvent listeners */
   enum af_{
      a_NONE,
      a_HISTOK = 1u<<0,
      a_USE_PROMPT = 1u<<1,
      a_USE_MLE = 1u<<2,
      a_DIG_MSG_OVERLAY = 1u<<16
   };

   struct n_string xprompt;
   FILE *ifile;
   char const *iftype;
   struct a_go_input_inject *giip;
   int nold, n;
   BITENUM_IS(u32, af_) f;
   NYD2_IN;

   if(!(gif & mx_GO_INPUT_HOLDALLSIGS))
      mx_sigs_all_holdx();

   f = a_NONE;

   if(a_go_ctx->gc_flags & a_GO_FORCE_EOF){
      a_go_ctx->gc_flags |= a_GO_IS_EOF;
      n = -1;
      goto jleave;
   }

   if(gif & mx_GO_INPUT_FORCE_STDIN)
      goto jforce_stdin;

   /* Special case macro mode: never need to prompt, lines have always been
    * unfolded already; TODO we need on_line_completed event and producers! */
   if(a_go_ctx->gc_flags & a_GO_MACRO){
      if(*linebuf != NIL)
         su_FREE(*linebuf);

      /* Injection in progress?  Do not care about the autocommit state here */
      if(!(gif & mx_GO_INPUT_DELAY_INJECTIONS) &&
            (giip = a_go_ctx->gc_inject) != NIL){
         a_go_ctx->gc_inject = giip->gii_next;

         /* Simply "reuse" allocation, copy string to front of it */
jinject:
         *linesize = giip->gii_len;
         *linebuf = S(char*,S(void*,giip));
         su_mem_move(*linebuf, giip->gii_dat, giip->gii_len +1);
         iftype = "INJECTION";
      }else{
         if((*linebuf = a_go_ctx->gc_lines[a_go_ctx->gc_loff]) == NIL){
            *linesize = 0;
            a_go_ctx->gc_flags |= a_GO_IS_EOF;
            n = -1;
            goto jleave;
         }

         ++a_go_ctx->gc_loff;
         *linesize = su_cs_len(*linebuf);
         if(!(a_go_ctx->gc_flags & a_GO_MACRO_FREE_DATA))
            *linebuf = su_cs_dup_cbuf(*linebuf, *linesize, 0);

         iftype = ((a_go_ctx->gc_flags &
                  (a_GO_MACRO_X_OPTION | a_GO_MACRO_BLTIN_RC))
               ? "COMMAND-LINE"
               : (a_go_ctx->gc_flags & a_GO_MACRO_CMD) ? "CMD" : "MACRO");
      }
      n = S(int,*linesize);
      n_pstate |= n_PS_READLINE_NL;
      goto jhave_dat;
   }

   if(!(gif & mx_GO_INPUT_DELAY_INJECTIONS)){
      /* Injection in progress? */
      struct a_go_input_inject **giipp;

      giipp = &a_go_ctx->gc_inject;

      if((giip = *giipp) != NIL){
         *giipp = giip->gii_next;

         if(giip->gii_commit){
            if(*linebuf != NIL)
               su_FREE(*linebuf);
            if(!giip->gii_no_history)
               f |= a_HISTOK;
            goto jinject; /* (above) */
         }else{
            string = savestrbuf(giip->gii_dat, giip->gii_len);
            su_FREE(giip);
         }
      }
   }

jforce_stdin:
   n_pstate &= ~n_PS_READLINE_NL;
   /* xxx once differentiated further if we `source'd */
   iftype = (n_psonce & n_PSO_STARTED) ? "READ" : "LOAD";
   if(!(n_pstate & n_PS_ROBOT) &&
         (n_psonce & (n_PSO_INTERACTIVE | n_PSO_STARTED)) ==
            (n_PSO_INTERACTIVE | n_PSO_STARTED))
      f |= a_HISTOK;
   if(!(f & a_HISTOK) || (gif & mx_GO_INPUT_FORCE_STDIN))
      gif |= mx_GO_INPUT_PROMPT_NONE;
   else{
      f |= a_USE_PROMPT;
      if(!ok_blook(line_editor_disable))
         f |= a_USE_MLE;
      else
         (void)n_string_creat_auto(&xprompt);
      if(prompt == NIL)
         gif |= mx_GO_INPUT_PROMPT_EVAL;
   }

   /* Ensure stdout is flushed first anyway (partial lines, maybe?) */
   if((gif & mx_GO_INPUT_PROMPT_NONE) && !(f & a_USE_MLE))
      fflush(n_stdout);

   if(gif & mx_GO_INPUT_FORCE_STDIN){
      struct a_go_readctl_ctx *grcp;
      struct mx_dig_msg_ctx *dmcp;

      if((dmcp = mx_dig_msg_read_overlay) != NIL){
         ifile = dmcp->dmc_fp;
         f |= a_DIG_MSG_OVERLAY;
      }else if((grcp = n_readctl_read_overlay) == NIL ||
            (ifile = grcp->grc_fp) == NIL)
         ifile = n_stdin;
   }else
      ifile = a_go_ctx->gc_file;
   if(ifile == NIL){
      ASSERT((n_pstate & n_PS_COMPOSE_FORKHOOK) &&
         (a_go_ctx->gc_flags & a_GO_MACRO));
      ifile = n_stdin;
   }

   for(nold = n = 0;;){
      if(f & a_USE_MLE){
         ASSERT(ifile == n_stdin);
         if(string != NIL && (n = S(int,su_cs_len(string))) > 0){
            if(*linesize > 0)
               *linesize += n +1;
            else
               *linesize = S(uz,n) + LINESIZE +1;
            *linebuf = su_MEM_REALLOC_LOCOR(*linebuf, *linesize,
                  su_DBG_LOC_ARGS_ORUSE);
           su_mem_copy(*linebuf, string, S(uz,n) +1);
         }
         string = NIL;

         mx_sigs_all_rele();

         n = (mx_tty_readline)(gif, prompt, linebuf, linesize, n, histok_or_nil
               su_DBG_LOC_ARGS_USE);

         mx_sigs_all_holdx();

         if(n < 0 && !ferror(ifile)) /* EOF never i guess */
            a_go_ctx->gc_flags |= a_GO_IS_EOF;
      }else{
         mx_sigs_all_rele();

         if(!(gif & mx_GO_INPUT_PROMPT_NONE)){
            mx_tty_create_prompt(&xprompt, prompt, gif);

            if(xprompt.s_len > 0){
               fwrite(xprompt.s_dat, 1, xprompt.s_len, n_stdout);
               fflush(n_stdout);
            }
         }

         n = (readline_restart)(ifile, linebuf, linesize, n
               su_DBG_LOC_ARGS_USE);

         mx_sigs_all_holdx();

         if(n < 0 && !ferror(ifile))
            a_go_ctx->gc_flags |= a_GO_IS_EOF;

         if(n > 0 && nold > 0){
            char const *cp;
            int i;

            i = 0;
            cp = &(*linebuf)[nold];
            while(su_cs_is_space(*cp) && n - i >= nold)
               ++cp, ++i;
            if(i > 0){
               su_mem_move(&(*linebuf)[nold], cp, n - nold - i);
               n -= i;
               (*linebuf)[n] = '\0';
            }
         }
      }
      if(n <= 0)
         break;

      /* POSIX says:
       * TODO We do NOT KNOW, thus no care for current shell quote mode!
       * TODO Thus "echo '\<NEWLINE HERE> bla' will never work
       *    An unquoted <backslash> at the end of a command line shall
       *    be discarded and the next line shall continue the command */
      if(!(gif & mx_GO_INPUT_NL_ESC) || (*linebuf)[n - 1] != '\\')
         break;

      /* Definitely outside of quotes, thus quoting rules are so that an uneven
       * number of successive reverse solidus at EOL is a continuation */
      if(n > 1){
         uz i, j;

         for(j = 1, i = S(uz,n) - 1; i-- > 0; ++j)
            if((*linebuf)[i] != '\\')
               break;
         if(!(j & 1))
            break;
      }
      (*linebuf)[nold = --n] = '\0';
      gif |= mx_GO_INPUT_NL_FOLLOW;
   }
   if(n < 0)
      goto jleave;

   (*linebuf)[*linesize = n] = '\0';

   if(f & a_USE_MLE)
      n_pstate |= n_PS_READLINE_NL;
   else if(n == 0 || su_cs_is_space(**linebuf))
      f &= ~a_HISTOK;

jhave_dat:
   if(n_poption & n_PO_D_VVV)
      n_err(_("%s%s %d bytes <%s>\n"),
         iftype, (mx_cnd_if_exists() == TRUM1 ? "?whiteout" : su_empty),
         n, *linebuf);

jleave:
   if (n_pstate & n_PS_PSTATE_PENDMASK)
      a_go_update_pstate();

   /* TODO We need to special case a_GO_SPLICE, since that is not managed by us
    * TODO but only established from the outside and we need to drop this
    * TODO overlay context somehow; ditto DIG_MSG_OVERLAY */
   if(n < 0){
      if(f & a_DIG_MSG_OVERLAY)
         mx_dig_msg_read_overlay = NIL;
      if(a_go_ctx->gc_flags & a_GO_SPLICE)
         a_go_cleanup(a_go_ctx,
            a_GO_CLEANUP_TEARDOWN | a_GO_CLEANUP_HOLDALLSIGS);
   }

   if(histok_or_nil != NIL && !(f & a_HISTOK))
      *histok_or_nil = FAL0;

   if(!(gif & mx_GO_INPUT_HOLDALLSIGS))
      mx_sigs_all_rele();

   NYD2_OU;
   return n;
}

char *
mx_go_input_cp(BITENUM_IS(u32,mx_go_input_flags) gif, char const *prompt,
      char const *string){
   struct n_sigman sm;
   boole histadd;
   uz linesize;
   char *linebuf, * volatile rv;
   int n;
   NYD2_IN;

   mx_fs_linepool_aquire(&linebuf, &linesize);
   rv = NIL;

   n_SIGMAN_ENTER_SWITCH(&sm, n_SIGMAN_ALL){
   case 0:
      break;
   default:
      goto jleave;
   }

   histadd = TRU1;
   n = mx_go_input(gif, prompt, &linebuf, &linesize, string, &histadd);
   if(n > 0 && *(rv = savestrbuf(linebuf, S(uz,n))) != '\0' &&
         (gif & mx_GO_INPUT_HIST_ADD) && (n_psonce & n_PSO_INTERACTIVE) &&
         histadd){
      ASSERT(!(gif & mx_GO_INPUT_HIST_ERROR) ||
         (gif & mx_GO_INPUT_HIST_GABBY));
      mx_tty_addhist(rv, gif);
   }

   n_sigman_cleanup_ping(&sm);

jleave:
   mx_fs_linepool_release(linebuf, linesize);

   NYD2_OU;
   n_sigman_leave(&sm, n_SIGMAN_VIPSIGS_NTTYOUT);
   return rv;
}

boole
mx_go_load_rc(char const *name){
   struct a_go_ctx *gcp;
   uz i;
   FILE *fip;
   boole rv;
   NYD_IN;
   ASSERT_NYD_EXEC(name != NIL, rv = FAL0);

   rv = TRU1;

   if((fip = mx_fs_open(name, "r")) == NIL){
      if(n_poption & n_PO_D_V)
         n_err(_("No such file to load: %s\n"), n_shexp_quote_cp(name, FAL0));
      goto jleave;
   }

   i = su_cs_len(name) +1;
   gcp = su_ALLOC(VSTRUCT_SIZEOF(struct a_go_ctx,gc_name) + i);
   su_mem_set(gcp, 0, VSTRUCT_SIZEOF(struct a_go_ctx,gc_name));

   gcp->gc_file = fip;
   gcp->gc_flags = a_GO_FREE | a_GO_FILE;
   su_mem_copy(gcp->gc_name, name, i);

   if(n_poption & n_PO_D_VV)
      n_err(_("Loading %s\n"), n_shexp_quote_cp(gcp->gc_name, FAL0));
   rv = a_go_load(gcp);

jleave:
   NYD_OU;
   return rv;
}

boole
mx_go_load_lines(boole injectit, char const **lines, uz cnt){
   static char const a_name_x[] = "-X", a_name_bltin[] = "builtin RC file";

   union{
      boole rv;
      u64 align;
      char uf[VSTRUCT_SIZEOF(struct a_go_ctx,gc_name) +
            MAX(sizeof(a_name_x), sizeof(a_name_bltin))];
   } b;
   char const *srcp, *xsrcp;
   char *cp;
   uz imax, i, len;
   boole nofail;
   struct a_go_ctx *gcp;
   NYD_IN;

   gcp = S(void*,b.uf);
   su_mem_set(gcp, 0, VSTRUCT_SIZEOF(struct a_go_ctx,gc_name));

   if(lines == NIL){
      su_mem_copy(gcp->gc_name, a_name_bltin, sizeof a_name_bltin);
      lines = C(char const**,a_go_bltin_rc_lines);
      cnt = a_GO_BLTIN_RC_LINES_CNT;
      gcp->gc_flags = a_GO_MACRO | a_GO_MACRO_BLTIN_RC |
            a_GO_MACRO_ROBOT | a_GO_MACRO_FREE_DATA;
      nofail = TRUM1;
   }else if(!injectit){
      su_mem_copy(gcp->gc_name, a_name_x, sizeof a_name_x);
      gcp->gc_flags = a_GO_MACRO | a_GO_MACRO_X_OPTION |
            a_GO_MACRO_ROBOT | a_GO_MACRO_FREE_DATA;
      nofail = FAL0;
   }else
      nofail = TRU1;

   /* The problem being that we want to support reverse solidus newline
    * escaping also within multiline -X, i.e., POSIX says:
    *    An unquoted <backslash> at the end of a command line shall
    *    be discarded and the next line shall continue the command
    * Therefore instead of "gcp->gc_lines = UNCONST(lines)", duplicate
    * the entire lines array and set _MACRO_FREE_DATA.
    * Likewise, for injections, we need to reverse the order. */
   imax = cnt + 1;
   gcp->gc_lines = su_ALLOC(sizeof(*gcp->gc_lines) * imax);

   /* For each of the input lines.. */
   for(i = len = 0, cp = NIL; cnt > 0;){
      boole keep;
      uz j;

      if((j = su_cs_len(srcp = *lines)) == 0){
         ++lines, --cnt;
         continue;
      }

      /* Separate one line from a possible multiline input string */
      if(nofail != TRUM1 && (xsrcp = su_mem_find(srcp, '\n', j)) != NIL){
         *lines = &xsrcp[1];
         j = P2UZ(xsrcp - srcp);
      }else
         ++lines, --cnt;

      /* The (separated) string may itself indicate soft newline escaping */
      if((keep = (srcp[j - 1] == '\\'))){
         uz xj, xk;

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
      if(cp != NIL)
         while(j > 0 && su_cs_is_space(*srcp))
            ++srcp, --j;

      if(j > 0){
         if(i + 2 >= imax){ /* TODO need a vector (main.c, here, ++) */
            imax += 4;
            gcp->gc_lines = su_REALLOC(gcp->gc_lines, sizeof(*gcp->gc_lines) *
                  imax);
         }
         gcp->gc_lines[i] = cp = su_REALLOC(cp, len + j +1);
         su_mem_copy(&cp[len], srcp, j);
         cp[len += j] = '\0';

         if(!keep)
            ++i;
      }
      if(!keep)
         cp = NIL, len = 0;
   }
   if(cp != NIL){
      ASSERT(i + 1 < imax);
      gcp->gc_lines[i++] = cp;
   }
   gcp->gc_lines[i] = NIL;

   if(!injectit)
      b.rv = a_go_load(gcp);
   else{
      while(i > 0){
         mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT, cp = gcp->gc_lines[--i],
            UZ_MAX);
         su_FREE(cp);
      }
      su_FREE(gcp->gc_lines);
      ASSERT(nofail);
   }

   if(nofail)
      /* Program exit handling is a total mess! */
      b.rv = ((n_psonce & n_PSO_EXIT_MASK) == 0);

   NYD_OU;
   return b.rv;
}

int
c_source(void *vp){
   int rv;
   NYD_IN;

   rv = (a_go_file(*S(char**,vp), FAL0) == TRU1) ? n_EXIT_OK : n_EXIT_ERR;

   NYD_OU;
   return rv;
}

int
c_source_if(void *vp){ /* XXX obsolete?, support file tests in `if' etc.! */
   int rv;
   NYD_IN;

   rv = (a_go_file(*S(char**,vp), TRU1) == TRU1) ? n_EXIT_OK : n_EXIT_ERR;

   NYD_OU;
   return rv;
}

boole
mx_go_macro(BITENUM_IS(u32,mx_go_input_flags) gif, char const *name,
      char **lines, void (*on_finalize)(void*), void *finalize_arg){
   struct a_go_ctx *gcp;
   uz i;
   int rv;
   sigset_t osigmask;
   NYD_IN;

   sigprocmask(SIG_BLOCK, NIL, &osigmask);

   gcp = su_ALLOC(VSTRUCT_SIZEOF(struct a_go_ctx,gc_name) +
         (i = su_cs_len(name) +1));
   su_mem_set(gcp, 0, VSTRUCT_SIZEOF(struct a_go_ctx,gc_name));
   gcp->gc_data.gdc_membag =
         su_mem_bag_create(&gcp->gc_data.gdc__membag_buf[0], 0);

   mx_sigs_all_holdx();

   gcp->gc_outer = a_go_ctx;
   gcp->gc_osigmask = osigmask;
   gcp->gc_flags = a_GO_FREE | a_GO_MACRO | a_GO_MACRO_FREE_DATA |
         a_GO_MACRO_ROBOT |
         ((a_go_ctx->gc_flags & a_GO_FILE_ROBOT) ? a_GO_FILE_ROBOT : 0) |
         ((gif & mx_GO_INPUT_NO_XCALL) ? a_GO_XCALL_IS_CALL : 0);
   gcp->gc_lines = lines;
   gcp->gc_on_finalize = on_finalize;
   gcp->gc_finalize_arg = finalize_arg;
   su_mem_copy(gcp->gc_name, name, i);

   a_go_ctx = gcp;
   mx_go_data = &gcp->gc_data;
   n_pstate |= n_PS_ROBOT;
   rv = a_go_event_loop(gcp, gif);

   /* Shall this enter a `xcall' stack avoidance optimization (loop) after
    * being teared down? */
   if(a_go_xcall != NIL){
      void *vp;
      struct mx_cmd_arg_ctx *cacp;

      if(a_go_xcall == R(void*,-1))
         a_go_xcall = NIL;
      else if(S(void const*,(cacp = a_go_xcall)->cac_indat) == gcp){
         /* Indicate that "our" (ex-) parent now hosts xcall optimization */
         a_go_ctx->gc_flags |= a_GO_XCALL_LOOP;

         while(a_go_xcall != NIL){
            mx_sigs_all_holdx();

            vp = a_go_xcall;
            a_go_xcall = NIL;
            cacp = mx_cmd_arg_restore_from_heap(vp);
            su_FREE(vp);

            a_go_ctx->gc_flags &= ~a_GO_XCALL_LOOP_ERROR;
            vp = a_go_ctx->gc_xcall_lopts;
            a_go_ctx->gc_xcall_lopts = NIL;

            mx_sigs_all_rele();

            (void)mx_xcall(cacp, vp);
         }
         rv = ((a_go_ctx->gc_flags & a_GO_XCALL_LOOP_ERROR) == 0);
         a_go_ctx->gc_flags &= ~a_GO_XCALL_LOOP_MASK;
      }
   }

   NYD_OU;
   return rv;
}

boole
mx_go_command(BITENUM_IS(u32,mx_go_input_flags) gif, char const *cmd){
   struct a_go_ctx *gcp;
   boole rv;
   uz i, ial;
   sigset_t osigmask;
   NYD_IN;

   sigprocmask(SIG_BLOCK, NIL, &osigmask);

   i = su_cs_len(cmd) +1;
   ial = Z_ALIGN(i);
   gcp = su_ALLOC(VSTRUCT_SIZEOF(struct a_go_ctx,gc_name) +
         ial + 2*sizeof(char*));
   su_mem_set(gcp, 0, VSTRUCT_SIZEOF(struct a_go_ctx,gc_name));
   gcp->gc_data.gdc_membag =
         su_mem_bag_create(&gcp->gc_data.gdc__membag_buf[0], 0);

   mx_sigs_all_holdx();

   gcp->gc_outer = a_go_ctx;
   gcp->gc_osigmask = osigmask;
   gcp->gc_flags = a_GO_FREE | a_GO_MACRO | a_GO_MACRO_CMD |
         ((a_go_ctx->gc_flags & a_GO_MACRO_ROBOT) ? a_GO_MACRO_ROBOT : 0) |
         ((a_go_ctx->gc_flags & a_GO_FILE_ROBOT) ? a_GO_FILE_ROBOT : 0);
   gcp->gc_lines = (void*)&gcp->gc_name[ial];
   su_mem_copy(gcp->gc_lines[0] = &gcp->gc_name[0], cmd, i);
   gcp->gc_lines[1] = NIL;

   a_go_ctx = gcp;
   mx_go_data = &gcp->gc_data;
   n_pstate |= n_PS_ROBOT;
   rv = a_go_event_loop(gcp, gif);

   NYD_OU;
   return rv;
}

void
mx_go_splice_hack(char const *cmd, FILE *new_stdin, FILE *new_stdout,
      u32 new_psonce, void (*on_finalize)(void*), void *finalize_arg){
   struct a_go_ctx *gcp;
   uz i;
   sigset_t osigmask;
   NYD_IN;

   sigprocmask(SIG_BLOCK, NIL, &osigmask);

   gcp = su_ALLOC(VSTRUCT_SIZEOF(struct a_go_ctx,gc_name) +
         (i = su_cs_len(cmd) +1));
   su_mem_set(gcp, 0, VSTRUCT_SIZEOF(struct a_go_ctx,gc_name));

   mx_sigs_all_holdx();

   gcp->gc_outer = a_go_ctx;
   gcp->gc_osigmask = osigmask;
   gcp->gc_file = new_stdin;
   gcp->gc_flags = a_GO_FREE | a_GO_SPLICE | a_GO_DATACTX_INHERITED;
   gcp->gc_on_finalize = on_finalize;
   gcp->gc_finalize_arg = finalize_arg;
   gcp->gc_splice_stdin = n_stdin;
   gcp->gc_splice_stdout = n_stdout;
   gcp->gc_splice_psonce = n_psonce;
   su_mem_copy(gcp->gc_name, cmd, i);

   n_stdin = new_stdin;
   n_stdout = new_stdout;
   n_psonce = new_psonce;
   a_go_ctx = gcp;
   /* Do NOT touch go_data! */
   n_pstate |= n_PS_ROBOT;

   mx_sigs_all_rele();

   NYD_OU;
}

void
mx_go_splice_hack_remove_after_jump(void){
   a_go_cleanup(a_go_ctx, a_GO_CLEANUP_TEARDOWN);
}

boole
mx_go_may_yield_control(void){ /* TODO this is a terrible hack */
   struct a_go_ctx *gcp;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   /* Only when startup completed */
   if(!(n_psonce & n_PSO_STARTED))
      goto jleave;
   /* Only interactive or batch mode (assuming that is ok) */
   if(!(n_psonce & n_PSO_INTERACTIVE) && !(n_poption & n_PO_BATCH_FLAG))
      goto jleave;

   /* Not when running any hook */
   if(n_pstate & n_PS_HOOK_MASK)
      goto jleave;

   /* Traverse up the stack:
    * . not when controlled by a child process
    * TODO . not when there are pipes involved, we neither handle job control,
    * TODO   nor process groups, that is, controlling terminal acceptably
    * . not when sourcing a file */
   for(gcp = a_go_ctx; gcp != NIL; gcp = gcp->gc_outer){
      if(gcp->gc_flags & (a_GO_PIPE | a_GO_FILE | a_GO_SPLICE))
         goto jleave;
   }

   rv = TRU1;
jleave:
   NYD2_OU;
   return rv;
}

int
c_eval(void *vp){
   /* TODO HACK! `eval' should be nothing else but a command prefix, evaluate
    * TODO ARGV with shell rules, but if that is not possible then simply
    * TODO adjust argv/argc of "the CmdCtx" that we will have exec real cmd */
   struct a_go_eval_ctx gec;
   struct n_string s_b, *s;
   uz i, j;
   char const **argv, *cp;
   NYD_IN;

   argv = vp;

   for(j = i = 0; (cp = argv[i]) != NIL; ++i)
      j += su_cs_len(cp);

   s = n_string_creat_auto(&s_b);
   s = n_string_reserve(s, j);

   for(i = 0; (cp = argv[i]) != NIL; ++i){
      if(i > 0)
         s = n_string_push_c(s, ' ');
      s = n_string_push_cp(s, cp);
   }

   su_mem_set(&gec, 0, sizeof gec);
   gec.gec_line.s = n_string_cp(s);
   gec.gec_line.l = s->s_len;
   if(n_poption & n_PO_D_VV)
      n_err(_("EVAL %" PRIuZ " bytes <%s>\n"), gec.gec_line.l, gec.gec_line.s);
   (void)/* XXX */a_go_evaluate(a_go_ctx, &gec);

   NYD_OU;
   return (a_go_xcall != NIL ? n_EXIT_OK : n_pstate_ex_no);
}

int
c_xcall(void *vp){
   int rv;
   struct a_go_ctx *gcp;
   NYD2_IN;

   /* The context can only be a macro context, except that possibly a single
    * level of `eval' (FIXME yet) was used to double-expand our arguments */
   if((gcp = a_go_ctx)->gc_flags & a_GO_MACRO_CMD)
      gcp = gcp->gc_outer;

   if((gcp->gc_flags & (a_GO_MACRO | a_GO_MACRO_X_OPTION |
         a_GO_MACRO_BLTIN_RC | a_GO_MACRO_CMD)) != a_GO_MACRO){
      if(n_poption & n_PO_D_V)
         n_err(_("xcall: can only be used inside a macro, using `call'\n"));
      rv = c_call(vp);
   }else{
      /* Try to roll up the stack as much as possible.
       * See a_GO_XCALL_LOOP flag description for more */
      /* xxx this gc_outer!=NIL should be redundant! assert it? */
      mx_go_input_force_eof();

      if(!(gcp->gc_flags & a_GO_XCALL_IS_CALL) && gcp->gc_outer != NIL){
         struct mx_cmd_arg_ctx *cacp;
         struct a_go_ctx *my, *outer;

         if((outer = (my = gcp)->gc_outer)->gc_flags & a_GO_XCALL_LOOP)
            gcp = outer;
         outer->gc_xcall_lopts = mx_xcall_exchange_lopts(my->gc_finalize_arg);

         cacp = mx_cmd_arg_save_to_heap(vp);
         cacp->cac_indat = S(char*,S(void*,gcp));
         a_go_xcall = cacp;

         rv = 0;
      }else{
         /* This mostly means we are being invoked from the outermost aka first
          * level of a hook or `account', so silently act like `call', then */
         rv = c_call(vp);
      }
   }

   NYD2_OU;
   return rv;
}

int
c_exit(void *vp){
   char const **argv;
   NYD_IN;

   if(*(argv = vp) != NIL && (su_idec_s32_cp(&n_exit_status, *argv, 0, NIL) &
            (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED)
      n_exit_status |= n_EXIT_ERR;

   if(n_pstate & n_PS_COMPOSE_FORKHOOK){ /* TODO sic */
      fflush(NIL);
      _exit(n_exit_status);
   }else if(n_pstate & n_PS_COMPOSE_MODE) /* XXX really.. */
      n_err(_("exit: delayed until compose mode is left\n")); /* XXX ..log? */

   n_psonce |= n_PSO_XIT;

   NYD_OU;
   return n_EXIT_OK;
}

int
c_quit(void *vp){
   char const **argv;
   NYD_IN;

   if(*(argv = vp) != NIL && (su_idec_s32_cp(&n_exit_status, *argv, 0, NIL) &
            (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED)
      n_exit_status |= n_EXIT_ERR;

   if(n_pstate & n_PS_COMPOSE_FORKHOOK){ /* TODO sic */
      fflush(NIL);
      _exit(n_exit_status);
   }else if(n_pstate & n_PS_COMPOSE_MODE) /* XXX really.. */
      n_err(_("quit: delayed until compose mode is left\n")); /* XXX ..log? */

   n_psonce |= n_PSO_QUIT;

   NYD_OU;
   return n_EXIT_OK;
}

int
c_readctl(void *vp){
   /* TODO We would need OnForkEvent and then simply remove some internal
    * TODO management; we do not have this, therefore we need global
    * TODO n_readctl_read_overlay to be accessible via =NIL, and to make that
    * TODO work in turn we need an instance for default STDIN!  Sigh. */
   static union{
      u64 alignme;
      u8 buf[VSTRUCT_SIZEOF(struct a_go_readctl_ctx,grc_name)+1 +1];
   } a;
   static struct a_go_readctl_ctx *a_stdin;

   struct a_go_readctl_ctx *grcp;
   char const *emsg;
   enum{
      a_NONE = 0,
      a_ERR = 1u<<0,
      a_SET = 1u<<1,
      a_CREATE = 1u<<2,
      a_REMOVE = 1u<<3
   } f;
   struct mx_cmd_arg *cap;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   if(a_stdin == NIL){
      a_stdin = S(struct a_go_readctl_ctx*,S(void*,a.buf));
      a_stdin->grc_name[0] = '-';
      n_readctl_read_overlay = a_stdin;
   }

   n_pstate_err_no = su_ERR_NONE;
   cacp = vp;
   cap = cacp->cac_arg;

   if(cacp->cac_no == 0 ||
         su_cs_starts_with_case("show", cap->ca_arg.ca_str.s))
      goto jshow;
   else if(su_cs_starts_with_case("set", cap->ca_arg.ca_str.s))
      f = a_SET;
   else if(su_cs_starts_with_case("create", cap->ca_arg.ca_str.s))
      f = a_CREATE;
   else if(su_cs_starts_with_case("remove", cap->ca_arg.ca_str.s))
      f = a_REMOVE;
   else{
      emsg = N_("readctl: invalid subcommand: %s\n");
      goto jeinval_quote;
   }

   if(cacp->cac_no == 1){ /* TODO better option parser <> subcommand */
      n_err(_("readctl: %s: requires argument\n"), cap->ca_arg.ca_str.s);
      goto jeinval;
   }
   cap = cap->ca_next;

   /* - is special TODO unfortunately also regarding storage */
   if(cap->ca_arg.ca_str.l == 1 && *cap->ca_arg.ca_str.s == '-'){
      if(f & (a_CREATE | a_REMOVE)){
         n_err(_("readctl: cannot create nor remove -\n"));
         goto jeinval;
      }
      n_readctl_read_overlay = a_stdin;
      goto jleave;
   }

   /* Try to find a yet existing instance */
   if((grcp = n_readctl_read_overlay) != NIL){
      for(; grcp != NIL; grcp = grcp->grc_next)
         if(!su_cs_cmp(grcp->grc_name, cap->ca_arg.ca_str.s))
            goto jfound;
      for(grcp = n_readctl_read_overlay; (grcp = grcp->grc_last) != NIL;)
         if(!su_cs_cmp(grcp->grc_name, cap->ca_arg.ca_str.s))
            goto jfound;
   }

   if(f & (a_SET | a_REMOVE)){
      emsg = N_("readctl: no such channel: %s\n");
      goto jeinval_quote;
   }

jfound:
   if(f & a_SET)
      n_readctl_read_overlay = grcp;
   else if(f & a_REMOVE){
      if(n_readctl_read_overlay == grcp)
         n_readctl_read_overlay = a_stdin;

      if(grcp->grc_last != NIL)
         grcp->grc_last->grc_next = grcp->grc_next;
      if(grcp->grc_next != NIL)
         grcp->grc_next->grc_last = grcp->grc_last;
      fclose(grcp->grc_fp);
      su_FREE(grcp);
   }else{
      FILE *fp;
      uz elen;
      s32 fd;

      if(grcp != NIL){
         n_err(_("readctl: channel already exists: %s\n"), /* TODO reopen */
            n_shexp_quote_cp(cap->ca_arg.ca_str.s, FAL0));
         n_pstate_err_no = su_ERR_EXIST;
         f = a_ERR;
         goto jleave;
      }

      if((su_idec_s32_cp(&fd, cap->ca_arg.ca_str.s, 0, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED){
         if((emsg = fexpand(cap->ca_arg.ca_str.s, (FEXP_LOCAL_FILE |
               FEXP_NVAR))) == NIL){
            emsg = N_("readctl: cannot expand filename %s\n");
            goto jeinval_quote;
         }
         fd = -1;
         elen = su_cs_len(emsg);
         fp = mx_fs_open(emsg, "&r");
      }else if(fd == STDIN_FILENO || fd == STDOUT_FILENO ||
            fd == STDERR_FILENO){
         emsg = N_("readctl: create: standard descriptors not allowed: %s\n");
         goto jeinval_quote;
      }else{
         /* xxx Avoid */
         if(!mx_FS_FD_CLOEXEC_SET(fd)){
            emsg = N_("readctl: create: "
                  "cannot set close-on-exec flag for: %s\n");
            goto jeinval_quote;
         }
         emsg = NIL;
         elen = 0;
         fp = fdopen(fd, "r");
      }

      if(fp != NIL){
         uz i;

         if((i = UZ_MAX - elen) <= cap->ca_arg.ca_str.l ||
               (i -= cap->ca_arg.ca_str.l) <=
                  VSTRUCT_SIZEOF(struct a_go_readctl_ctx,grc_name) +2){
            fclose(fp);
            n_err(_("readctl: failed to create storage for %s\n"),
               cap->ca_arg.ca_str.s);
            n_pstate_err_no = su_ERR_OVERFLOW;
            f = a_ERR;
            goto jleave;
         }

         grcp = su_ALLOC(VSTRUCT_SIZEOF(struct a_go_readctl_ctx,grc_name) +
               cap->ca_arg.ca_str.l +1 + elen +1);
         grcp->grc_last = NIL;
         if((grcp->grc_next = n_readctl_read_overlay) != NIL)
            grcp->grc_next->grc_last = grcp;
         n_readctl_read_overlay = grcp;
         grcp->grc_fp = fp;
         grcp->grc_fd = fd;
         su_mem_copy(grcp->grc_name, cap->ca_arg.ca_str.s,
            cap->ca_arg.ca_str.l +1);
         if(elen == 0)
            grcp->grc_expand = NIL;
         else{
            char *cp;

            grcp->grc_expand = cp = &grcp->grc_name[cap->ca_arg.ca_str.l +1];
            su_mem_copy(cp, emsg, ++elen);
         }
      }else{
         emsg = N_("readctl: failed to create file for %s\n");
         goto jeinval_quote;
      }
   }

jleave:
   NYD_OU;
   return (f & a_ERR) ? n_EXIT_ERR : n_EXIT_OK;

jeinval_quote:
   n_err(V_(emsg), n_shexp_quote_cp(cap->ca_arg.ca_str.s, FAL0));
jeinval:
   n_pstate_err_no = su_ERR_INVAL;
   f = a_ERR;
   goto jleave;

jshow:
   if((grcp = n_readctl_read_overlay) == NIL)
      fprintf(n_stdout, _("readctl: no channels registered\n"));
   else{
      while(grcp->grc_last != NIL)
         grcp = grcp->grc_last;

      fprintf(n_stdout, _("readctl: registered channels:\n"));
      for(; grcp != NIL; grcp = grcp->grc_next)
         fprintf(n_stdout, _("%c%s %s%s%s%s\n"),
            (grcp == n_readctl_read_overlay ? '*' : ' '),
            (grcp->grc_fd != -1 ? _("descriptor") : _("name")),
            n_shexp_quote_cp(grcp->grc_name, FAL0),
            (grcp->grc_expand != NIL ? " (" : su_empty),
            (grcp->grc_expand != NIL ? grcp->grc_expand : su_empty),
            (grcp->grc_expand != NIL ? ")" : su_empty));
   }
   f = a_NONE;
   goto jleave;
}

#include "su/code-ou.h"
/* s-it-mode */
