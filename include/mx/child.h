/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Child process handling, direct (pipe streams are in file-streams.h).
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CHILD_H
#define mx_CHILD_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_child_ctx;

/* */
#define mx_CHILD_MAXARGS 3 /* xxx vector */

enum mx_child_flags{
   mx_CHILD_NONE = 0,
   /* Ensure the forked child has started running (as opposed to the default
    * success which only remarks "fork(2) has returned successfully") */
   mx_CHILD_SPAWN_CONTROL = 1u<<0,
   /* On top of SPAWN_CONTROL, wait until the child called execve */
   mx_CHILD_SPAWN_CONTROL_LINGER = 1u<<1,
   /* run(): handle all the lifetime, and wait etc., return final status */
   mx_CHILD_RUN_WAIT_LIFE = 1u<<2,

   /* Child uses terminal and requires termios handling */
   mx__CHILD_JOBCTL = 1u<<8
};

/* mx_child_ctx_setup() to zero out */
struct mx_child_ctx{
   /* Output: */
   s32 cc_pid; /* Filled upon successful start (when not waited..) */
   s32 cc_exit_status; /* WEXITSTATUS(); -WEXITSTATUS() if !WIFEXITED */
   /* err_no() on failure; with SPAWN_CONTROL it could be set to errors that
    * happen in the child, too; set to ERR_CHILD when there was error and there
    * was no error */
   s32 cc_error;
   u32 cc_flags; /* child_flags */
   /* Input: the arguments are all optional */
   /* Signals must be handled by the caller.  If this is set, then it is
    * a sigset_t*: the signals to ignore in the new process.
    * SIGINT is enabled unless it is in the mask. */
   void *cc_mask; /* In fact a sigset_t XXX su_sigset! */
   s32 cc_fds[2]; /* A real FD, or _FD_PASS or _FD_NULL XXX sz[2] */
   char const *cc_cmd; /* The command to run, */
   char const *cc_args[mx_CHILD_MAXARGS]; /* and its optional arguments */
   /* NIL or NIL terminated array; Note: is modified if set! */
   char const **cc_env_addon; /* TODO su_dict */
   sz cc__cpipe[2];
};

/* Slots in .cc_fds */
#define mx_CHILD_FD_IN 0
#define mx_CHILD_FD_OUT 1

/* Special file descriptors for .cc_fds */
#define mx_CHILD_FD_PASS (-1)
#define mx_CHILD_FD_NULL (-2)

/* At program startup: initialize controller (panic on failure) */
EXPORT void mx_child_controller_setup(void);

/* Initialize (zero out etc.).  The .cc_fds are set to CHILD_FD_PASS */
EXPORT void mx_child_ctx_setup(struct mx_child_ctx *ccp);

/* Very often "sh -c -- cmd_string" is to be executed.
 * sh_or_nil defaults to ok_vlook(SHELL) */
EXPORT void mx_child_ctx_set_args_for_sh(struct mx_child_ctx *ccp,
      char const *sh_or_nil, char const *cmd_string);

/* Start and run a command, with optional arguments and splicing of stdin and
 * stdout, as defined by the ctx_setup()d ccp, return whether the process has
 * been started successfully.
 * With RUN_WAIT_LIFE the return value also indicates whether the parent has
 * child_wait()ed successfully, i.e., whether the child has terminated
 * already; .cc_exit_status etc. can be examined for more.
 * Otherwise _signal(), _forget() and _wait() can be used on ccp */
EXPORT boole mx_child_run(struct mx_child_ctx *ccp);

/* Fork a child process, "enable" the below functions upon success.
 * With SPAWN_CONTROL the parent will linger until the child has called
 * in_child_setup() or even (with SPAWN_CONTROL_LINGER) until it execve's.
 * Children can start children themselves, but we do not care about termios
 * XXX or child handling no more in recursive levels */
EXPORT boole mx_child_fork(struct mx_child_ctx *ccp);

/* Setup an image in the child; signals are still blocked before that! */
EXPORT void mx_child_in_child_setup(struct mx_child_ctx *ccp);

/* This can only be used if SPAWN_CONTROL_LINGER had been used.
 * It will pass err up to the parent, and close the control pipe.
 * It does not exit the program */
EXPORT void mx_child_in_child_exec_failed(struct mx_child_ctx *ccp, s32 err);

/* Send a signal to a managed process, return 0 on success, a negative value
 * if the process does no(t) (longer) exist, or an error constant */
EXPORT s32 mx_child_signal(struct mx_child_ctx *ccp, s32 sig);

/* Loose any knowledge we might have regarding ccp.
 * Neither waiting nor any other status report will be available.
 * This must not be used in conjunction with FD_PASS. */
EXPORT void mx_child_forget(struct mx_child_ctx *ccp);

/* Wait on the child and return whether ccp was a known child and has been
 * waited for successfully; examine ccp for error / status */
EXPORT boole mx_child_wait(struct mx_child_ctx *ccp);

#include <su/code-ou.h>
#endif /* mx_CHILD_H */
/* s-it-mode */
