/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of cmd-spam.h.
 *
 * Copyright (c) 2013 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE cmd_spam
#define mx_SOURCE
#define mx_SOURCE_CMD_SPAM

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

su_EMPTY_FILE()
#ifdef mx_HAVE_SPAM

#include <su/cs.h>
#include <su/icodec.h>
#include <su/mem.h>
#include <su/mem-bag.h>

#ifdef mx_HAVE_REGEX
# include <su/re.h>
#endif

#include "mx/child.h"
#include "mx/file-streams.h"
#include "mx/random.h"
#include "mx/sigs.h"

#include "mx/cmd-spam.h"
#include "su/code-in.h"

/* This is chosen rather arbitrarily.
 * It must be able to swallow the first line of a rate response */
#if BUFFER_SIZE < 1024
# error *spam-interface* BUFFER_SIZE constraints are not matched
#endif

enum a_spam_action{
   a_SPAM_RATE,
   a_SPAM_HAM,
   a_SPAM_SPAM,
   a_SPAM_FORGET
};

#if defined mx_HAVE_SPAM_FILTER || defined mx_HAVE_SPAM_SPAMC
struct a_spam_cf{
   char const *cf_cmd;
   char *cf_result; /* _SPAM_RATE: first response line */
   s32 cf_exit_status;
   u8 cf__pad[3];
   boole cf_useshell;
   /* .cf_cmd may be adjusted for each call (`spamforget')... */
   char const *cf_acmd;
   char const *cf_a0;
   char const *cf_env[4];
   n_sighdl_t cf_otstp;
   n_sighdl_t cf_ottin;
   n_sighdl_t cf_ottou;
   n_sighdl_t cf_ohup;
   n_sighdl_t cf_opipe;
   n_sighdl_t cf_oint;
   n_sighdl_t cf_oquit;
};
#endif

#ifdef mx_HAVE_SPAM_FILTER
struct a_spam_filter{
   struct a_spam_cf f_super;
   char const *f_cmd_nospam; /* Working relative to current message.. */
   char const *f_cmd_noham;
# ifdef mx_HAVE_REGEX
   u8 f__pad[4];
   u32 f_score_grpno; /* 0 for not set */
   struct su_re f_score_regex;
# endif
};
#endif

#ifdef mx_HAVE_SPAM_SPAMC
struct a_spam_spamc{
   struct a_spam_cf c_super;
   char const *c_cmd_arr[9];
};
#endif

struct a_spam_vc{
   enum a_spam_action vc_action;
   boole vc_verbose; /* Verbose output */
   boole vc_progress; /* "Progress meter" (mutual verbose) */
   u8 vc__pad[2];
   boole (*vc_act)(struct a_spam_vc *);
   void (*vc_dtor)(struct a_spam_vc *);
   char *vc_buffer; /* I/O buffer, BUFFER_SIZE bytes */
   uz vc_mno; /* Current message number */
   struct message *vc_mp; /* Current message */
   FILE *vc_ifp; /* Input stream on .vc_mp */
   union{
#ifdef mx_HAVE_SPAM_FILTER
      struct a_spam_filter filter;
#endif
#ifdef mx_HAVE_SPAM_SPAMC
      struct a_spam_spamc spamc;
#endif
#if defined mx_HAVE_SPAM_FILTER || defined mx_HAVE_SPAM_SPAMC
      struct a_spam_cf cf;
#endif
   } vc_t;
   char const *vc_esep; /* Error separator for progress mode */
};

/* Indices according to enum spam_action */
static char const a_spam_cmds[][16] = {
   "spamrate", "spamham", "spamspam", "spamforget\0"
};

/* Shared action setup */
static boole a_spam_action(enum a_spam_action sa, int *ip);

/* *spam-interface*=filter: initialize, communicate */
#ifdef mx_HAVE_SPAM_FILTER
static boole a_spamfilter_setup(struct a_spam_vc *vcp);
static boole a_spamfilter_interact(struct a_spam_vc *vcp);
static void a_spamfilter_dtor(struct a_spam_vc *vcp);
#endif

/* *spam-interface*=spamc: initialize, communicate */
#ifdef mx_HAVE_SPAM_SPAMC
static boole a_spamc_setup(struct a_spam_vc *vcp);
static boole a_spamc_interact(struct a_spam_vc *vcp);
static void a_spamc_dtor(struct a_spam_vc *vcp);
#endif

/* *spam-interface*=(spamc|filter): create child + communication */
#if defined mx_HAVE_SPAM_FILTER || defined mx_HAVE_SPAM_SPAMC
static void a_spam_cf_setup(struct a_spam_vc *vcp, boole useshell);
static boole a_spam_cf_interact(struct a_spam_vc *vcp);
#endif

/* Convert a floating-point spam rate into message.m_spamscore */
#if (defined mx_HAVE_SPAM_FILTER && defined mx_HAVE_REGEX) ||\
      defined mx_HAVE_SPAM_SPAMC
static void a_spam_rate2score(struct a_spam_vc *vcp, char *buf);
#endif

static boole
a_spam_action(enum a_spam_action sa, int *ip){
   struct a_spam_vc vc;
   uz maxsize, skipped, cnt, curr;
   char const *cp;
   boole ok;
   NYD_IN;

   ok = FAL0;

   su_mem_set(&vc, 0, sizeof vc);
   vc.vc_action = sa;
   vc.vc_verbose = ((n_poption & n_PO_D_V) != 0);
   vc.vc_progress = (!vc.vc_verbose && ((n_psonce & n_PSO_INTERACTIVE) != 0));
   vc.vc_esep = vc.vc_progress ? "\n" : su_empty;

   /* Check and setup the desired spam interface */
   if((cp = ok_vlook(spam_interface)) == NIL){
      n_err(_("%s: no *spam-interface* set\n"), a_spam_cmds[sa]);
      goto jleave;
#ifdef mx_HAVE_SPAM_FILTER
   }else if(!su_cs_cmp_case(cp, "filter")){
      if(!a_spamfilter_setup(&vc))
         goto jleave;
#endif
#ifdef mx_HAVE_SPAM_SPAMC
   }else if(!su_cs_cmp_case(cp, "spamc")){
       if(!a_spamc_setup(&vc))
         goto jleave;
#endif
   }else{
      n_err(_("%s: unknown / unsupported *spam-interface*: %s\n"),
         a_spam_cmds[sa], cp);
      goto jleave;
   }

#if defined mx_HAVE_SPAM_FILTER || defined mx_HAVE_SPAM_SPAMC
   /* *spam-maxsize* we do handle ourselves instead */
   if((cp = ok_vlook(spam_maxsize)) == NIL ||
         (su_idec_u32_cp(&maxsize, cp, 0, NIL), maxsize) == 0)
      maxsize = SPAM_MAXSIZE;

   /* Finally get an I/O buffer */
   vc.vc_buffer = su_AUTO_ALLOC(BUFFER_SIZE);

   skipped = cnt = 0;
   if(vc.vc_progress){
      while(ip[cnt] != 0)
         ++cnt;
   }
   for(curr = 0, ok = TRU1; *ip != 0; --cnt, ++curr, ++ip){
      vc.vc_mno = S(uz,*ip);
      vc.vc_mp = &message[vc.vc_mno - 1];
      if(sa == a_SPAM_RATE)
         vc.vc_mp->m_spamscore = 0;

      if(vc.vc_mp->m_size > maxsize){
         if(vc.vc_verbose)
            n_err(_("%s: message %" PRIuZ " exceeds maxsize "
                  "(% " PRIuZ " > %" PRIuZ "), skip\n"),
               a_spam_cmds[sa], vc.vc_mno, S(uz,vc.vc_mp->m_size), maxsize);
         else if(vc.vc_progress){
            fprintf(n_stdout, "\r%s: !%-6" PRIuZ " %6" PRIuZ "/%-6" PRIuZ,
               a_spam_cmds[sa], vc.vc_mno, cnt, curr);
            fflush(n_stdout);
         }
         ++skipped;
      }else{
         if(vc.vc_verbose)
            n_err(_("%s: message %" PRIuZ "\n"), a_spam_cmds[sa], vc.vc_mno);
         else if(vc.vc_progress){
            fprintf(n_stdout, "\r%s: .%-6" PRIuZ " %6" PRIuZ "/%-6" PRIuZ,
               a_spam_cmds[sa], vc.vc_mno, cnt, curr);
            fflush(n_stdout);
         }

         setdot(vc.vc_mp, FAL0);
         if((vc.vc_ifp = setinput(&mb, vc.vc_mp, NEED_BODY)) == NIL){
            n_err(_("%s`%s': cannot load message %" PRIuZ ": %s\n"),
               vc.vc_esep, a_spam_cmds[sa], vc.vc_mno,
               su_err_doc(su_err_no()));
            ok = FAL0;
            break;
         }

         if(!(ok = (*vc.vc_act)(&vc)))
            break;
      }
   }
   if(vc.vc_progress){
      if(curr > 0)
         fprintf(n_stdout, _(" %s (%" PRIuZ "/%" PRIuZ " all/skipped)\n"),
            (ok ? _("done") : V_(n_error)), curr, skipped);
      fflush(n_stdout);
   }

   if(vc.vc_dtor != NIL)
      (*vc.vc_dtor)(&vc);
#endif /* defined mx_HAVE_SPAM_FILTER || defined mx_HAVE_SPAM_SPAMC */

jleave:
   NYD_OU;
   return ok;
}

#ifdef mx_HAVE_SPAM_FILTER
static boole
a_spamfilter_setup(struct a_spam_vc *vcp){
   char const *cp, *var;
   struct a_spam_filter *sfp;
   boole rv;
   NYD2_IN;

   rv = FAL0;
   sfp = &vcp->vc_t.filter;

   switch(vcp->vc_action){
   case a_SPAM_RATE:
      cp = ok_vlook(spamfilter_rate);
      var = "spam-filter-rate";
      goto jonecmd;
   case a_SPAM_HAM:
      cp = ok_vlook(spamfilter_ham);
      var = "spam-filter-ham";
      goto jonecmd;
   case a_SPAM_SPAM:
      cp = ok_vlook(spamfilter_spam);
      var = "spam-filter-spam";
jonecmd:
      if(cp == NIL){
jecmd:
         n_err(_("%s: *%s* is not set\n"), a_spam_cmds[vcp->vc_action], var);
         goto jleave;
      }
      sfp->f_super.cf_cmd = savestr(cp);
      break;
   case a_SPAM_FORGET:
      var = "spam-filter-nospam";
      if((cp =  ok_vlook(spamfilter_nospam)) == NIL)
         goto jecmd;
      sfp->f_cmd_nospam = savestr(cp);
      if((cp =  ok_vlook(spamfilter_noham)) == NIL)
         goto jecmd;
      sfp->f_cmd_noham = savestr(cp);
      break;
   }

# ifdef mx_HAVE_REGEX
   if(vcp->vc_action == a_SPAM_RATE &&
         (cp = ok_vlook(spamfilter_rate_scanscore)) != NIL){
      char const *bp;

      var = su_cs_find_c(cp, ';');
      if(var == NIL){
         n_err(_("%s: *spamfilter-rate-scanscore*: missing semicolon;: %s\n"),
            a_spam_cmds[vcp->vc_action], cp);
         goto jleave;
      }
      bp = &var[1];

      if((su_idec(&sfp->f_score_grpno, cp, P2UZ(var - cp), 0,
                  su_IDEC_MODE_LIMIT_32BIT, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED){
         n_err(_("%s: *spamfilter-rate-scanscore*: bad group: %s\n"),
            a_spam_cmds[vcp->vc_action], cp);
         goto jleave;
      }

      if(sfp->f_score_grpno == 0){
         n_err(_("%s: *spamfilter-rate-scanscore*: bad group 0: %s\n"),
            a_spam_cmds[vcp->vc_action], cp);
         goto jleave;
      }else if(su_re_setup_cp(su_re_create(&sfp->f_score_regex), bp,
            (su_RE_SETUP_EXT | su_RE_SETUP_ICASE)) != su_RE_ERROR_NONE){
         n_err(_("%s: invalid *spamfilter-rate-scanscore* regex: %s: %s\n"),
            a_spam_cmds[vcp->vc_action], n_shexp_quote_cp(cp, FAL0),
            su_re_error_doc(&sfp->f_score_regex));
         goto jerr_re;
      }else if(sfp->f_score_grpno > sfp->f_score_regex.re_group_count){
         n_err(_("%s: *spamfilter-rate-scanscore*: no group %u: %s\n"),
            a_spam_cmds[vcp->vc_action], sfp->f_score_grpno, cp);
jerr_re:
         su_re_gut(&sfp->f_score_regex);
         goto jleave;
      }
   }
# endif /* mx_HAVE_REGEX */

   a_spam_cf_setup(vcp, TRU1);

   vcp->vc_act = &a_spamfilter_interact;
   vcp->vc_dtor = &a_spamfilter_dtor;

   rv = TRU1;
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_spamfilter_interact(struct a_spam_vc *vcp){
# ifdef mx_HAVE_REGEX
   struct su_re_match *remp;
   struct a_spam_filter *sfp;
   char *cp;
# endif
   boole rv;
   NYD2_IN;

   if(vcp->vc_action == a_SPAM_FORGET)
      vcp->vc_t.cf.cf_cmd = (vcp->vc_mp->m_flag & MSPAM)
            ? vcp->vc_t.filter.f_cmd_nospam : vcp->vc_t.filter.f_cmd_noham;

   if(!(rv = a_spam_cf_interact(vcp)))
      goto jleave;

   vcp->vc_mp->m_flag &= ~(MSPAM | MSPAMUNSURE);
   if(vcp->vc_action != a_SPAM_RATE){
      if (vcp->vc_action == a_SPAM_SPAM)
         vcp->vc_mp->m_flag |= MSPAM;
      goto jleave;
   }else switch(vcp->vc_t.filter.f_super.cf_exit_status){
   case 2:
      vcp->vc_mp->m_flag |= MSPAMUNSURE;
      /* FALLTHRU */
   case 1:
      break;
   case 0:
      vcp->vc_mp->m_flag |= MSPAM;
      break;
   default:
      rv = FAL0;
      goto jleave;
   }

# ifdef mx_HAVE_REGEX
   sfp = &vcp->vc_t.filter;

   /* In case this became disabled.. */
   if(sfp->f_score_grpno == 0)
      goto jleave;

   if(sfp->f_super.cf_result == NIL){
      n_err(_("%s: *spamfilter-rate-scanscore*: filter does not "
         "produce output!\n"));
      goto jleave;
   }

   remp = &sfp->f_score_regex.re_match[sfp->f_score_grpno];

   if(!su_re_eval_cp(&sfp->f_score_regex, sfp->f_super.cf_result,
         su_RE_EVAL_NONE) || remp->rem_start == -1){
      n_err(_("%s: *spamfilter-rate-scanscore* "
            "does not match filter output!\n"), a_spam_cmds[vcp->vc_action]);
      su_re_gut(&sfp->f_score_regex);
      sfp->f_score_grpno = 0;
      goto jleave;
   }

   cp = sfp->f_super.cf_result;
   cp[remp->rem_end] = '\0';
   cp += remp->rem_start;
   a_spam_rate2score(vcp, cp);
# endif /* mx_HAVE_REGEX */

jleave:
   NYD2_OU;
   return rv;
}

static void
a_spamfilter_dtor(struct a_spam_vc *vcp){
   struct a_spam_filter *sfp;
   NYD2_IN;

   sfp = &vcp->vc_t.filter;

   if(sfp->f_super.cf_result != NIL)
      su_FREE(sfp->f_super.cf_result);

# ifdef mx_HAVE_REGEX
   if(sfp->f_score_grpno > 0)
      su_re_gut(&sfp->f_score_regex);
# endif

   NYD2_OU;
}
#endif /* mx_HAVE_SPAM_FILTER */

#ifdef mx_HAVE_SPAM_SPAMC
static boole
a_spamc_setup(struct a_spam_vc *vcp){
   struct str str;
   char const **args, *cp;
   struct a_spam_spamc *sscp;
   boole rv;
   NYD2_IN;

   rv = FAL0;
   sscp = &vcp->vc_t.spamc;
   args = sscp->c_cmd_arr;

   if((cp = ok_vlook(spamc_command)) == NIL){
# ifdef SPAM_SPAMC_PATH
      cp = SPAM_SPAMC_PATH;
# else
      n_err(_("%s: *spamc-command* is not set\n"),
         a_spam_cmds[vcp->vc_action]);
      goto jleave;
# endif
   }
   *args++ = cp;

   switch(vcp->vc_action){
   case a_SPAM_RATE:
      *args = "-c";
      break;
   case a_SPAM_HAM:
      args[1] = "ham";
      goto jlearn;
   case a_SPAM_SPAM:
      args[1] = "spam";
      goto jlearn;
   case a_SPAM_FORGET:
      args[1] = "forget";
jlearn:
      *args = "-L";
      ++args;
      break;
   }
   ++args;

   *args++ = "-l"; /* --log-to-stderr */
   *args++ = "-x"; /* No "safe callback", we need to react on errors! */

   if((cp = ok_vlook(spamc_arguments)) != NIL)
      *args++ = cp;

   if((cp = ok_vlook(spamc_user)) != NIL){
      if(*cp == '\0')
         cp = ok_vlook(LOGNAME);
      *args++ = "-u";
      *args++ = cp;
   }
   ASSERT(P2UZ(args - sscp->c_cmd_arr) <= NELEM(sscp->c_cmd_arr));

   *args = NIL;
   sscp->c_super.cf_cmd = str_concat_cpa(&str, sscp->c_cmd_arr, " ")->s;
   if(vcp->vc_verbose)
      n_err(_("spamc(1) via %s\n"),
         n_shexp_quote_cp(sscp->c_super.cf_cmd, FAL0));

   a_spam_cf_setup(vcp, FAL0);

   vcp->vc_act = &a_spamc_interact;
   vcp->vc_dtor = &a_spamc_dtor;

   rv = TRU1;
# ifndef SPAM_SPAMC_PATH
jleave:
# endif
   NYD2_OU;
   return rv;
}

static boole
a_spamc_interact(struct a_spam_vc *vcp){
   boole rv;
   NYD2_IN;

   if(!(rv = a_spam_cf_interact(vcp)))
      goto jleave;

   vcp->vc_mp->m_flag &= ~(MSPAM | MSPAMUNSURE);
   if(vcp->vc_action != a_SPAM_RATE){
      if(vcp->vc_action == a_SPAM_SPAM)
         vcp->vc_mp->m_flag |= MSPAM;
   }else{
      char *buf, *cp;

      switch(vcp->vc_t.spamc.c_super.cf_exit_status){
      case 1:
         vcp->vc_mp->m_flag |= MSPAM;
         /* FALLTHRU */
      case 0:
         break;
      default:
         rv = FAL0;
         goto jleave;
      }

      if((cp = su_cs_find_c(buf = vcp->vc_t.spamc.c_super.cf_result, '/')
            ) != NIL)
         buf[P2UZ(cp - buf)] = '\0';
      a_spam_rate2score(vcp, buf);
   }

jleave:
   NYD2_OU;
   return rv;
}

static void
a_spamc_dtor(struct a_spam_vc *vcp){
   NYD2_IN;

   if(vcp->vc_t.spamc.c_super.cf_result != NIL)
      su_FREE(vcp->vc_t.spamc.c_super.cf_result);

   NYD2_OU;
}
#endif /* mx_HAVE_SPAM_SPAMC */

#if defined mx_HAVE_SPAM_FILTER || defined mx_HAVE_SPAM_SPAMC
static void
a_spam_cf_setup(struct a_spam_vc *vcp, boole useshell){
   struct str s;
   char const *cp;
   struct a_spam_cf *scfp;
   NYD2_IN;
   LCTA(2 < NELEM(scfp->cf_env), "Preallocated buffer too small");

   scfp = &vcp->vc_t.cf;

   if((scfp->cf_useshell = useshell)){
      scfp->cf_acmd = ok_vlook(SHELL);
      scfp->cf_a0 = "-c";
   }

   /* MAILX_FILENAME_GENERATED *//* TODO pathconf NAME_MAX; but user can create
    * TODO a file wherever he wants!  *Do* create a zero-size temporary file
    * TODO and give *that* path as MAILX_FILENAME_TEMPORARY, clean it up once
    * TODO the pipe returns?  Like this we *can* verify path/name issues! */
   cp = mx_random_create_cp(MIN(NAME_MAX / 4, 16), NIL);
   scfp->cf_env[0] = str_concat_csvl(&s,
         n_PIPEENV_FILENAME_GENERATED, "=", cp, NIL)->s;
   /* v15 compat NAIL_ environments vanish! */
   scfp->cf_env[1] = str_concat_csvl(&s,
         "NAIL_FILENAME_GENERATED", "=", cp, NIL)->s;
   scfp->cf_env[2] = NIL;

   NYD2_OU;
}

static sigjmp_buf a__spam_cf_actjmp; /* TODO someday, we won't need it */
static int volatile a__spam_cf_sig; /* TODO someday, we won't need it */
static void
a__spam_cf_onsig(int sig){ /* TODO someday, we won't need it no more */
   NYD; /* Signal handler */
   a__spam_cf_sig = sig;
   siglongjmp(a__spam_cf_actjmp, 1);
}

static boole
a_spam_cf_interact(struct a_spam_vc *vcp){
   struct mx_child_ctx cc;
   struct a_spam_cf *scfp;
   sz p2c[2], c2p[2];
   sigset_t cset;
   char const *cp;
   uz size;
   enum{
      a_NONE = 0,
      a_SIGHOLD = 1u<<0,
      a_P2C_0 = 1u<<1,
      a_P2C_1 = 1u<<2,
      a_P2C = a_P2C_0 | a_P2C_1,
      a_C2P_0 = 1u<<3,
      a_C2P_1 = 1u<<4,
      a_C2P = a_C2P_0 | a_C2P_1,
      a_JUMPED = 1u<<5,
      a_RUNNING = 1u<<6,
      a_GOODRUN = 1u<<7,
      a_ERRORS = 1u<<8
   } volatile state = a_NONE;
   NYD2_IN;

   scfp = &vcp->vc_t.cf;
   if(scfp->cf_result != NIL){
      su_FREE(scfp->cf_result);
      scfp->cf_result = NIL;
   }

   /* TODO Avoid that we jump away; yet necessary signal mess */
   /*__spam_cf_sig = 0;*/
   hold_sigs();
   state |= a_SIGHOLD;
   scfp->cf_otstp = safe_signal(SIGTSTP, SIG_DFL);
   scfp->cf_ottin = safe_signal(SIGTTIN, SIG_DFL);
   scfp->cf_ottou = safe_signal(SIGTTOU, SIG_DFL);
   scfp->cf_opipe = safe_signal(SIGPIPE, SIG_IGN);
   scfp->cf_ohup = safe_signal(SIGHUP, &a__spam_cf_onsig);
   scfp->cf_oint = safe_signal(SIGINT, &a__spam_cf_onsig);
   scfp->cf_oquit = safe_signal(SIGQUIT, &a__spam_cf_onsig);
   /* Keep sigs blocked */

   if(!mx_fs_pipe_cloexec(p2c)){
      n_err(_("%s`%s': cannot create parent communication pipe: %s\n"),
         vcp->vc_esep, a_spam_cmds[vcp->vc_action], su_err_doc(su_err_no()));
      goto jtail;
   }
   state |= a_P2C;

   if(!mx_fs_pipe_cloexec(c2p)){
      n_err(_("%s`%s': cannot create child pipe: %s\n"),
         vcp->vc_esep, a_spam_cmds[vcp->vc_action], su_err_doc(su_err_no()));
      goto jtail;
   }
   state |= a_C2P;

   if(sigsetjmp(a__spam_cf_actjmp, 1)){
      if(*vcp->vc_esep != '\0')
         n_err(vcp->vc_esep);
      state |= a_JUMPED;
      goto jtail;
   }
   rele_sigs();
   state &= ~a_SIGHOLD;

   /* Start our command as requested */
   sigemptyset(&cset);
   mx_child_ctx_setup(&cc);
   cc.cc_mask = &cset;
   cc.cc_fds[mx_CHILD_FD_IN] = p2c[0];
   cc.cc_fds[mx_CHILD_FD_OUT] = c2p[1];
   cc.cc_cmd = (scfp->cf_acmd != NIL ? scfp->cf_acmd : scfp->cf_cmd);
   cc.cc_args[0] = scfp->cf_a0;
   if(scfp->cf_acmd != NIL)
      cc.cc_args[1] = scfp->cf_cmd;
   cc.cc_env_addon = scfp->cf_env;

   if(!mx_child_run(&cc)){
      state |= a_ERRORS;
      goto jtail;
   }
   state |= a_RUNNING;
   close(p2c[0]);
   state &= ~a_P2C_0;

   /* Yes, we could sendmp(SEND_MBOX), but simply passing through the MBOX
    * content does the same in effect, however much more efficiently.
    * XXX NOTE: this may mean we pass a message without From_ line! */
   for(size = vcp->vc_mp->m_size; size > 0;){
      uz i;

      i = fread(vcp->vc_buffer, 1, MIN(size, BUFFER_SIZE), vcp->vc_ifp);
      if(i == 0){
         if(ferror(vcp->vc_ifp))
            state |= a_ERRORS;
         break;
      }
      size -= i;
      if(i != S(uz,write(S(int,p2c[1]), vcp->vc_buffer, i))){
         state |= a_ERRORS;
         break;
      }
   }

jtail:
   /* TODO Quite racy -- block anything for a while? */
   if(state & a_SIGHOLD){
      state &= ~a_SIGHOLD;
      rele_sigs();
   }

   if(state & a_P2C_0){
      state &= ~a_P2C_0;
      close(S(int,p2c[0]));
   }
   if(state & a_C2P_1){
      state &= ~a_C2P_1;
      close(S(int,c2p[1]));
   }
   /* And cause EOF for the reader */
   if(state & a_P2C_1){
      state &= ~a_P2C_1;
      close(S(int,p2c[1]));
   }

   if(state & a_RUNNING){
      if(!(state & a_ERRORS) &&
            vcp->vc_action == a_SPAM_RATE && !(state & (a_JUMPED | a_ERRORS))){
         sz i;

         i = read(S(int,c2p[0]), vcp->vc_buffer, BUFFER_SIZE - 1);
         if(i > 0){
            vcp->vc_buffer[i] = '\0';
            if((cp = su_cs_find_c(vcp->vc_buffer, NETNL[0])) == NIL &&
                  (cp = su_cs_find_c(vcp->vc_buffer, NETNL[1])) == NIL){
               n_err(_("%s`%s': program generates too much output: %s\n"),
                  vcp->vc_esep, a_spam_cmds[vcp->vc_action],
                  n_shexp_quote_cp(scfp->cf_cmd, FAL0));
               state |= a_ERRORS;
            }else{
               scfp->cf_result = su_cs_dup_cbuf(vcp->vc_buffer,
                     P2UZ(cp - vcp->vc_buffer), 0);
/* FIXME consume child output until EOF??? */
            }
         }else if(i != 0)
            state |= a_ERRORS;
      }

      state &= ~a_RUNNING;
      if(mx_child_wait(&cc) && (scfp->cf_exit_status = cc.cc_exit_status) >= 0)
         state |= a_GOODRUN;
   }

   if(state & a_C2P_0){
      state &= ~a_C2P_0;
      close(S(int,c2p[0]));
   }

   safe_signal(SIGQUIT, scfp->cf_oquit);
   safe_signal(SIGINT, scfp->cf_oint);
   safe_signal(SIGHUP, scfp->cf_ohup);
   safe_signal(SIGPIPE, scfp->cf_opipe);
   safe_signal(SIGTSTP, scfp->cf_otstp);
   safe_signal(SIGTTIN, scfp->cf_ottin);
   safe_signal(SIGTTOU, scfp->cf_ottou);

   NYD2_OU;
   if(state & a_JUMPED){
      ASSERT(vcp->vc_dtor != NIL);
      (*vcp->vc_dtor)(vcp);

      sigemptyset(&cset);
      sigaddset(&cset, a__spam_cf_sig);
      sigprocmask(SIG_UNBLOCK, &cset, NIL);
      n_raise(a__spam_cf_sig);
   }
   return !(state & (a_JUMPED | a_ERRORS));
}
#endif /* mx_HAVE_SPAM_FILTER || mx_HAVE_SPAM_SPAMC */

#if (defined mx_HAVE_SPAM_FILTER && defined mx_HAVE_REGEX) ||\
      defined mx_HAVE_SPAM_SPAMC
static void
a_spam_rate2score(struct a_spam_vc *vcp, char *buf){
   u32 m, s;
   BITENUM_IS(u32,su_idec_state) ids;
   NYD2_IN;

   /* C99 */{ /* Overcome ISO C / compiler weirdness */
      char const *cp;

      cp = buf;
      ids = su_idec_u32_cp(&m, buf, 10, &cp);
      if((ids & su_IDEC_STATE_EMASK) & ~su_IDEC_STATE_EBASE)
         goto jleave;
      buf = UNCONST(char*,cp);
   }

   s = 0;
   if(!(ids & su_IDEC_STATE_CONSUMED)){
      /* Floating-point rounding for non-mathematicians */
      char c1, c2, c3;

      ++buf; /* '.' */
      if((c1 = buf[0]) != '\0' && (c2 = buf[1]) != '\0' &&
            (c3 = buf[2]) != '\0'){
         buf[2] = '\0';
         if(c3 >= '5'){
            if(c2 == '9'){
               if(c1 == '9'){
                  ++m;
                  goto jscore_ok;
               }else
                  buf[0] = ++c1;
               c2 = '0';
            }else
               ++c2;
            buf[1] = c2;
         }
      }

      ids = su_idec_u32_cp(&s, buf, 10, NIL);
      if((ids & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) != su_IDEC_STATE_CONSUMED)
         goto jleave;
   }

jscore_ok:
   vcp->vc_mp->m_spamscore = (m << 8) | s;
jleave:
   NYD2_OU;
}
#endif /* (mx_HAVE_SPAM_FILTER && mx_HAVE_REGEX) || mx_HAVE_SPAM_SPAMC */

int
c_spam_clear(void *vp){
   int *ip;
   NYD_IN;

   for(ip = vp; *ip != 0; ++ip)
      message[S(uz,*ip) - 1].m_flag &= ~(MSPAM | MSPAMUNSURE);

   NYD_OU;
   return n_EXIT_OK;
}

int
c_spam_set(void *vp){
   int *ip;
   NYD_IN;

   for(ip = vp; *ip != 0; ++ip){
      message[S(uz,*ip) - 1].m_flag &= ~(MSPAM | MSPAMUNSURE);
      message[S(uz,*ip) - 1].m_flag |= MSPAM;
   }

   NYD_OU;
   return n_EXIT_OK;
}

int
c_spam_forget(void *vp){
   int rv;
   NYD_IN;

   rv = a_spam_action(a_SPAM_FORGET, S(int*,vp)) ? n_EXIT_OK : n_EXIT_ERR;

   NYD_OU;
   return rv;
}

int
c_spam_ham(void *vp){
   int rv;
   NYD_IN;

   rv = a_spam_action(a_SPAM_HAM, S(int*,vp)) ? n_EXIT_OK : n_EXIT_ERR;

   NYD_OU;
   return rv;
}

int
c_spam_rate(void *vp){
   int rv;
   NYD_IN;

   rv = a_spam_action(a_SPAM_RATE, S(int*,vp)) ? n_EXIT_OK : n_EXIT_ERR;

   NYD_OU;
   return rv;
}

int
c_spam_spam(void *vp){
   int rv;
   NYD_IN;

   rv = a_spam_action(a_SPAM_SPAM, S(int*,vp)) ? n_EXIT_OK : n_EXIT_ERR;

   NYD_OU;
   return rv;
}

#include "su/code-ou.h"
#endif /* mx_HAVE_SPAM */
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_CMD_SPAM
/* s-it-mode */
