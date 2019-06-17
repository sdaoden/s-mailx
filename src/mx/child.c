/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of child.h.
 *@ TODO . argument and environment space constraints not tested.
 *@ TODO . use a SU child, offer+use our own stuff for "wait status" checks.
 *@ TODO (requires event loop then, likely)
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#define su_FILE child
#define mx_SOURCE
#define mx_SOURCE_CHILD

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <sys/wait.h>

#include <su/cs.h>
#include <su/mem.h>

#include "mx/file-streams.h"
#include "mx/termcap.h"

#include "mx/child.h"
#include "su/code-in.h"

struct a_child_ent{
   struct a_child_ent *ce_link;
   s32 ce_pid; /* -1: struct can be gc'd */
   s32 ce_status; /* wait status */
   boole ce_done; /* has terminated */
   boole ce_forget; /* will not be wait()ed upon */
   boole ce_jobctl; /* Counts against child_jobctl_users */
   u8 ce__dummy[5];
};

static struct a_child_ent *a_child_head;

/* Cleanup internal structures which have been rendered obsolete (childs have
 * terminated) in the meantime; returns list to be freed.
 * Note: signals need to be blocked when calling this */
static struct a_child_ent *a_child_manager_cleanup(void);

/* It or NIL is returned */
SINLINE struct a_child_ent *a_child_find(s32 pid,
      struct a_child_ent ***lppp_or_nil);

/* Handle SIGCHLD */
static void a_child__sigchld(int signo);

static struct a_child_ent *
a_child_manager_cleanup(void){
   struct a_child_ent *nlp, **nlpp, **cepp, *cep;
   NYD_IN;

   nlp = NIL;
   nlpp = &nlp;

   for(cepp = &a_child_head; *cepp != NIL;){
      if((*cepp)->ce_pid == -1){
         cep = *cepp;
         *cepp = cep->ce_link;

         *nlpp = cep;
         nlpp = &cep->ce_link;
      }else
         cepp = &(*cepp)->ce_link;
   }
   NYD_OU;
   return nlp;
}

SINLINE struct a_child_ent *
a_child_find(s32 pid, struct a_child_ent ***lppp_or_nil){
   struct a_child_ent **cepp, *cep;
   NYD2_IN;

   for(cepp = &a_child_head; (cep = *cepp) != NIL; cepp = &(*cepp)->ce_link)
      if(cep->ce_pid == pid)
         break;

   if(lppp_or_nil != NIL)
      *lppp_or_nil = cepp;
   NYD2_OU;
   return cep;
}

static void
a_child__sigchld(int signo){
   struct a_child_ent *cep;
   int status;
   pid_t pid;
   UNUSED(signo);

   for(;;){
      pid = waitpid(-1, &status, WNOHANG);
      if(pid <= 0){
         if(pid == -1 && su_err_no() == su_ERR_INTR)
            continue;
         break;
      }

      if((cep = a_child_find(S(s32,pid), NIL)) != NIL){
         cep->ce_done = TRU1;
         cep->ce_status = status;
         if(cep->ce_forget)
            cep->ce_pid = -1;
      }
   }
}

void
mx_child_manager_start(void){
   struct sigaction nact, oact;
   NYD_IN;

   nact.sa_handler = &a_child__sigchld;
   sigemptyset(&nact.sa_mask);
   nact.sa_flags = SA_RESTART
#ifdef SA_NOCLDSTOP
         | SA_NOCLDSTOP
#endif
         ;

   if(sigaction(SIGCHLD, &nact, &oact) != 0)
      n_panic(_("Cannot install signal handler for child process management"));
   NYD_OU;
}

void
mx_child_ctx_setup(struct mx_child_ctx *ccp){
   NYD2_IN;
   ASSERT(ccp);

   su_mem_set(ccp, 0, sizeof *ccp);
   ccp->cc_fds[0] = ccp->cc_fds[1] = mx_CHILD_FD_PASS;
   NYD2_OU;
}

boole
mx_child_run(struct mx_child_ctx *ccp){
   s32 e;
   NYD_IN;

   ASSERT(ccp);
   ASSERT(ccp->cc_pid == 0);
   ASSERT(!(ccp->cc_flags & mx_CHILD_SPAWN_CONTROL_LINGER) ||
      (ccp->cc_flags & mx_CHILD_SPAWN_CONTROL));

   e = su_ERR_NONE;

   if(!mx_child_fork(ccp))
      e = ccp->cc_error;
   else if(ccp->cc_pid == 0)
      goto jchild;
   else if(ccp->cc_flags & mx_CHILD_RUN_WAIT_LIFE){
      if(!mx_child_wait(ccp))
         e = ccp->cc_error;

      if((e != su_ERR_NONE || ccp->cc_exit_status < 0) &&
            (ok_blook(bsdcompat) || ok_blook(bsdmsgs)))
         n_err(_("Fatal error in process\n"));
   }

   if(e != su_ERR_NONE){
      n_perr(_("child_run()"), e);
      su_err_set_no(ccp->cc_error = e);
   }

   NYD_OU;
   return (e == su_ERR_NONE);

jchild:{
   char *argv[128 + 4]; /* TODO magic constant, fixed size -> su_vector */
   int i;

   if(ccp->cc_env_addon != NIL){
      extern char **environ;
      uz ei, ei_orig, ai, ai_orig;
      char **env;
      char const **env_addon;

      env_addon = ccp->cc_env_addon;

      /* TODO note we don't check the POSIX limit:
       * the total space used to store the environment and the arguments to
       * the process is limited to {ARG_MAX} bytes */
      for(ei = 0; environ[ei] != NIL; ++ei)
         ;
      ei_orig = ei;
      for(ai = 0; env_addon[ai] != NIL; ++ai)
         ;
      ai_orig = ai;
      env = n_lofi_alloc(sizeof(*env) * (ei + ai +1));
      su_mem_copy(env, environ, sizeof(*env) * ei);

      /* Replace all those keys that yet exist */
      while(ai-- > 0){
         char const *ee, *kvs;
         uz kl;

         ee = env_addon[ai];
         kvs = su_cs_find_c(ee, '=');
         ASSERT(kvs != NIL);
         kl = P2UZ(kvs - ee);
         ASSERT(kl > 0);
         for(ei = ei_orig; ei-- > 0;){
            char const *ekvs;

            if((ekvs = su_cs_find_c(env[ei], '=')) != NIL &&
                  kl == P2UZ(ekvs - env[ei]) && !su_mem_cmp(ee, env[ei], kl)){
               env[ei] = UNCONST(char*,ee);
               env_addon[ai] = NIL;
               break;
            }
         }
      }

      /* And append the rest */
      for(ei = ei_orig, ai = ai_orig; ai-- > 0;)
         if(env_addon[ai] != NIL)
            env[ei++] = UNCONST(char*,env_addon[ai]);

      env[ei] = NIL;
      environ = env;
   }

   i = (int)getrawlist(TRU1, argv, NELEM(argv) - 4, ccp->cc_cmd,
         su_cs_len(ccp->cc_cmd));
   if(i >= 0){
      if((argv[i++] = UNCONST(char*,ccp->cc_args[0])) != NIL &&
            (argv[i++] = UNCONST(char*,ccp->cc_args[1])) != NIL &&
            (argv[i++] = UNCONST(char*,ccp->cc_args[2])) != NIL)
         argv[i] = NIL;

      mx_child_in_child_setup(ccp);

      execvp(argv[0], argv);
      perror(argv[0]);
   }
   _exit(n_EXIT_ERR);
   }
}

boole
mx_child_fork(struct mx_child_ctx *ccp){
   sigset_t nset, oset;
   struct a_child_ent *nlp, *cep;
   NYD_IN;

   ASSERT(ccp);
   ASSERT(ccp->cc_pid == 0);
   ASSERT(!(ccp->cc_flags & mx_CHILD_SPAWN_CONTROL_LINGER) ||
      (ccp->cc_flags & mx_CHILD_SPAWN_CONTROL));
   ASSERT(ccp->cc_error == su_ERR_NONE);

   if(n_poption & n_PO_D_V)
      n_err(_("Forking child%s: %s %s %s %s\n"),
         (ccp->cc_flags & mx_CHILD_SPAWN_CONTROL ? _(" with spawn control")
            : su_empty),
         n_shexp_quote_cp(((ccp->cc_cmd != R(char*,-1)
            ? (ccp->cc_cmd != NIL ? ccp->cc_cmd : _("exec handled by caller"))
            : _("forked concurrent \"in-image\" code"))), FAL0),
         (ccp->cc_args[0] != NIL ? n_shexp_quote_cp(ccp->cc_args[0], FAL0)
            : su_empty),
         (ccp->cc_args[1] != NIL ? n_shexp_quote_cp(ccp->cc_args[1], FAL0)
            : su_empty),
         (ccp->cc_args[2] != NIL ? n_shexp_quote_cp(ccp->cc_args[2], FAL0)
            : su_empty));

   if((ccp->cc_flags & mx_CHILD_SPAWN_CONTROL) &&
         !mx_fs_pipe_cloexec(&ccp->cc__cpipe[0])){
      ccp->cc_error = su_err_no();
      goto jleave;
   }

   cep = su_TCALLOC(struct a_child_ent, 1);

   sigfillset(&nset);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   nlp = a_child_manager_cleanup();

   switch((ccp->cc_pid = cep->ce_pid = fork())){
   case 0:
      goto jkid;
   case -1:
      ccp->cc_error = su_err_no();
      sigprocmask(SIG_SETMASK, &oset, NIL);
      if(ccp->cc_flags & mx_CHILD_SPAWN_CONTROL){
         close(S(int,ccp->cc__cpipe[0]));
         close(S(int,ccp->cc__cpipe[1]));
      }
      n_perr(_("child_fork(): fork failure"), ccp->cc_error);
      /* Link in cleanup list on failure */
      cep->ce_link = nlp;
      nlp = cep;
      break;
   default:
      cep->ce_link = a_child_head;
      a_child_head = cep;

      /* In the parent, conditionally wait on the control pipe */
      if(ccp->cc_flags & mx_CHILD_SPAWN_CONTROL){
         char ebuf[sizeof(ccp->cc_error)];
         sz r;

         close(S(int,ccp->cc__cpipe[1]));
         r = read(S(int,ccp->cc__cpipe[0]), ebuf, sizeof ebuf);
         close(S(int,ccp->cc__cpipe[0]));

         if(r == sizeof(ccp->cc_error))
            su_mem_copy(&ccp->cc_error, ebuf, sizeof(ccp->cc_error));
         else if(r != 0)
            ccp->cc_error = su_ERR_CHILD;
      }

      /* in_child_setup() will procmask() for the child */
      sigprocmask(SIG_SETMASK, &oset, NIL);
      break;
   }

   /* Free all stale childs */
   while(nlp != NIL){
      cep = nlp;
      nlp = nlp->ce_link;
      su_FREE(cep);
   }

jleave:
   NYD_OU;
   return (ccp->cc_error == su_ERR_NONE);

jkid:
   /* Close the unused end of the control pipe right now */
   if(ccp->cc_flags & mx_CHILD_SPAWN_CONTROL)
      close(S(int,ccp->cc__cpipe[0]));
   goto jleave;
}

void
mx_child_in_child_setup(struct mx_child_ctx *ccp){
   sigset_t fset;
   s32 fd, i;
   ASSERT(ccp);
   ASSERT(ccp->cc_pid == 0);

   /* All file descriptors other than 0, 1, and 2 are supposed to be cloexec */
   /* TODO WHAT IS WITH STDERR_FILENO DAMMIT? */
   if((i = ((fd = S(s32,ccp->cc_fds[0])) == mx_CHILD_FD_NULL)))
      ccp->cc_fds[0] = fd = open(n_path_devnull, O_RDONLY);
   if(fd >= 0){
      dup2(fd, STDIN_FILENO);
      if(i)
         close(fd);
   }

   if((i = ((fd = S(s32,ccp->cc_fds[1])) == mx_CHILD_FD_NULL)))
      ccp->cc_fds[1] = fd = open(n_path_devnull, O_WRONLY);
   if(fd >= 0){
      dup2(fd, STDOUT_FILENO);
      if(i)
         close(fd);
   }

   /* Close our side of the control pipe, unless we should wait for cloexec to
    * do that for us */
   if((ccp->cc_flags & (mx_CHILD_SPAWN_CONTROL | mx_CHILD_SPAWN_CONTROL_LINGER)
         ) == mx_CHILD_SPAWN_CONTROL)
      close(S(int,ccp->cc__cpipe[1]));

   if(ccp->cc_mask != NIL){
      sigset_t *ssp;

      ssp = S(sigset_t*,ccp->cc_mask);
      for(i = 1; i < NSIG; ++i)
         if(sigismember(ssp, i))
            safe_signal(i, SIG_IGN);
      if(!sigismember(ssp, SIGINT))
         safe_signal(SIGINT, SIG_DFL);
   }

   sigemptyset(&fset);
   sigprocmask(SIG_SETMASK, &fset, NIL);
}

void
mx_child_in_child_exec_failed(struct mx_child_ctx *ccp, s32 err){
   ASSERT(ccp);
   ASSERT(ccp->cc_pid == 0);
   ASSERT(ccp->cc_flags & mx_CHILD_SPAWN_CONTROL_LINGER);

   ccp->cc_error = err;
   (void)write(S(int,ccp->cc__cpipe[1]), &ccp->cc_error,
      sizeof(ccp->cc_error));
   close(S(int,ccp->cc__cpipe[1]));
}

s32
mx_child_signal(struct mx_child_ctx *ccp, s32 sig){
   s32 rv;
   struct a_child_ent *cep;
   NYD_IN;

   ASSERT(ccp);
   ASSERT(ccp->cc_pid > 0);

   if((cep = a_child_find(ccp->cc_pid, NIL)) != NIL){
      ASSERT(!cep->ce_forget);
      if(cep->ce_done)
         rv = -1;
      else if((rv = kill(S(pid_t,ccp->cc_pid), sig)) != 0)
         rv = su_err_no();
   }else
      ccp->cc_pid = rv = -1;

   NYD_OU;
   return rv;
}

void
mx_child_forget(struct mx_child_ctx *ccp){
   sigset_t nset, oset;
   struct a_child_ent *cep, **cepp;
   NYD_IN;

   ASSERT(ccp);
   ASSERT(ccp->cc_pid > 0);

   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   if((cep = a_child_find(ccp->cc_pid, &cepp)) != NIL){
      ASSERT(!cep->ce_forget);
      ASSERT(!(ccp->cc_flags & mx__CHILD_JOBCTL));
      cep->ce_forget = TRU1;
      if(cep->ce_done)
         *cepp = cep->ce_link;
   }

   sigprocmask(SIG_SETMASK, &oset, NIL);

   if(cep != NIL && cep->ce_done)
      su_FREE(cep);

   ccp->cc_pid = -1;
   NYD_OU;
}

boole
mx_child_wait(struct mx_child_ctx *ccp){
   sigset_t oset, nset;
   s32 ws;
   boole ok;
   struct a_child_ent *cep, **cepp;
   NYD_IN;

   ASSERT(ccp);
   ASSERT(ccp->cc_pid > 0);

   sigemptyset(&nset);
   sigaddset(&nset, SIGCHLD);
   sigprocmask(SIG_BLOCK, &nset, &oset);

   ok = TRU1;
   if((cep = a_child_find(ccp->cc_pid, &cepp)) != NIL){
      while(!cep->ce_done &&
            waitpid(S(pid_t,ccp->cc_pid), &cep->ce_status, 0) == -1){
         if((ws = su_err_no()) != su_ERR_INTR && ws != su_ERR_CHILD){
            ok = FAL0;
            break;
         }
      }

      ws = cep->ce_status;
      *cepp = cep->ce_link;
   }else{
      cepp = R(struct a_child_ent**,-1);
      ws = 0;
   }

   sigprocmask(SIG_SETMASK, &oset, NIL);

   if(cep != NIL)
      su_FREE(cep);

   if(ok){
      ccp->cc_exit_status = WEXITSTATUS(ws);
      if(!WIFEXITED(ws) && ccp->cc_exit_status > 0)
         ccp->cc_exit_status = -ccp->cc_exit_status;
   }else{
      cep = NIL;
      ccp->cc_exit_status = -255;
   }

   ccp->cc_pid = -1;
   NYD_OU;
   return (cep != NIL);
}

#include "su/code-ou.h"
/* s-it-mode */
