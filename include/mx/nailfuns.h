/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Function prototypes and function-alike macros.
 *@ TODO Should be split in myriads of FEATURE-GROUP.h headers.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2020 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: BSD-3-Clause TODO ISC
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

struct mx_attachment;
struct mx_cmd_arg;
struct su_cs_dict;
struct quoteflt;

/*
 * TODO Convert optional utility+ functions to n_*(); ditto
 * TODO else use generic module-specific prefixes: str_(), am[em]_, sm[em]_, ..
 */
/* TODO s-it-mode: not really (docu, funnames, funargs, etc) */

#undef FL
#ifndef mx_HAVE_AMALGAMATION
# define FL extern
#else
# define FL static
#endif

/*
 * Macro-based generics
 */

/* RFC 822, 3.2. */
#define fieldnamechar(c) \
   (su_cs_is_ascii(c) && (c) > 040 && (c) != 0177 && (c) != ':')

/* Single-threaded, use unlocked I/O */
#ifdef mx_HAVE_PUTC_UNLOCKED
# undef getc
# define getc(c) getc_unlocked(c)
# undef putc
# define putc(c, f) putc_unlocked(c, f)
#endif

/* There are problems with dup()ing of file-descriptors for child processes.
 * We have to somehow accomplish that the FILE* fp makes itself comfortable
 * with the *real* offset of the underlying file descriptor.
 * POSIX Issue 7 overloaded fflush(3): if used on a readable stream, then
 *
 *    if the file is not already at EOF, and the file is one capable of
 *    seeking, the file offset of the underlying open file description shall
 *    be set to the file position of the stream */
#if !su_OS_OPENBSD &&\
   defined _POSIX_VERSION && _POSIX_VERSION + 0 >= 200809L
# define n_real_seek(FP,OFF,WH) (fseek(FP, OFF, WH) != -1 && fflush(FP) != EOF)
# define really_rewind(stream) \
do{\
   rewind(stream);\
   fflush(stream);\
}while(0)

#else
# define n_real_seek(FP,OFF,WH) \
   (fseek(FP, OFF, WH) != -1 && fflush(FP) != EOF &&\
      lseek(fileno(FP), OFF, WH) != -1)
# define really_rewind(stream) \
do{\
   rewind(stream);\
   fflush(stream);\
   lseek(fileno(stream), 0, SEEK_SET);\
}while(0)
#endif

/* fflush() and rewind() */
#define fflush_rewind(stream) \
do{\
   fflush(stream);\
   rewind(stream);\
}while(0)

/* Truncate a file to the last character written.  This is useful just before
 * closing an old file that was opened for read/write */
#define ftrunc(stream) \
do{\
   off_t off;\
   fflush(stream);\
   off = ftell(stream);\
   if(off >= 0)\
      ftruncate(fileno(stream), off);\
}while(0)

/*
 * accmacvar.c
 */

/* Macros: `define', `undefine', `call', `call_if' */
FL int c_define(void *v);
FL int c_undefine(void *v);
FL int c_call(void *v);
FL int c_call_if(void *v);

/* Accounts: `account', `unaccount' */
FL void mx_account_leave(void);
FL int c_account(void *v);
FL int c_unaccount(void *v);

/* `localopts', `shift', `return' */
FL int c_localopts(void *vp);
FL int c_shift(void *vp);
FL int c_return(void *vp);

/* TODO - Main loop on tick event: mx_sigs_all_holdx() is active
 * TODO - main.c *on-program-exit*
 * mac must not be NIL */
FL void temporary_on_xy_hook_caller(char const *hname, char const *mac,
      boole sigs_held);

/* TODO Check whether a *folder-hook* exists for currently active mailbox */
FL boole temporary_folder_hook_check(boole nmail);
FL void temporary_folder_hook_unroll(void); /* XXX im. hack */

/* TODO v15 drop Invoke compose hook macname */
FL void temporary_compose_mode_hook_call(char const *macname,
            void (*hook_pre)(void *), void *hook_arg);
FL void temporary_compose_mode_hook_unroll(void);

#ifdef mx_HAVE_HISTORY
/* TODO *on-history-addition* */
FL boole temporary_addhist_hook(char const *ctx, char const *gabby_type,
            char const *histent);
#endif

/* TODO v15 drop: let shexp_parse_token take a carrier with positional
 * TODO params, then let callers use that as such!!
 * Call hook in a recursed environment named name where positional params are
 * setup according to argv/argc.  NOTE: all signals blocked! */
#ifdef mx_HAVE_REGEX
FL char *temporary_pospar_access_hook(char const *name, char const **argv,
      u32 argc, char *(*hook)(void *uservp), void *uservp);
#endif

/* Setting up batch mode, variable-handling side */
FL void n_var_setup_batch_mode(void);

/* Can name freely be used as a variable by users? */
FL boole n_var_is_user_writable(char const *name);

/* Don't use n_var_* unless you *really* have to! */

/* Constant option key look/(un)set/clear */
FL char *n_var_oklook(enum okeys okey);
#define ok_blook(C) (n_var_oklook(su_CONCAT(ok_b_, C)) != NULL)
#define ok_vlook(C) n_var_oklook(su_CONCAT(ok_v_, C))

FL boole n_var_okset(enum okeys okey, up val);
#define ok_bset(C) \
   n_var_okset(su_CONCAT(ok_b_, C), (up)TRU1)
#define ok_vset(C,V) \
   n_var_okset(su_CONCAT(ok_v_, C), (up)(V))

FL boole n_var_okclear(enum okeys okey);
#define ok_bclear(C) n_var_okclear(su_CONCAT(ok_b_, C))
#define ok_vclear(C) n_var_okclear(su_CONCAT(ok_v_, C))

/* Variable option key lookup/(un)set/clear.
 * If try_getenv is true we will getenv(3) _if_ vokey is not a known enum okey;
 * it will also cause obsoletion messages only for doing lookup (once).
 * _vexplode() is to be used by the shell expansion stuff when encountering
 * ${@} in double-quotes, in order to provide sh(1)ell compatible behaviour;
 * it returns whether there are any elements in argv (*cookie) */
FL char const *n_var_vlook(char const *vokey, boole try_getenv);
FL boole n_var_vexplode(void const **cookie);
FL boole n_var_vset(char const *vokey, up val);
FL boole n_var_vclear(char const *vokey);

/* Special case to handle the typical [xy-USER@HOST,] xy-HOST and plain xy
 * variable chains; oxm is a bitmix which tells which combinations to test */
#ifdef mx_HAVE_NET
FL char *n_var_xoklook(enum okeys okey, struct mx_url const *urlp,
            enum okey_xlook_mode oxm);
# define xok_BLOOK(C,URL,M) (n_var_xoklook(C, URL, M) != NULL)
# define xok_VLOOK(C,URL,M) n_var_xoklook(C, URL, M)
# define xok_blook(C,URL,M) xok_BLOOK(su_CONCAT(ok_b_, C), URL, M)
# define xok_vlook(C,URL,M) xok_VLOOK(su_CONCAT(ok_v_, C), URL, M)
#endif

/* User variable access: `set', `local' and `unset' */
FL int c_set(void *vp);
FL int c_unset(void *vp);

/* `varshow' */
FL int c_varshow(void *v);

/* `environ' */
FL int c_environ(void *v);

/* `vpospar' */
FL int c_vpospar(void *v);

/*
 * auxlily.c
 */

/* Compute *screen* size */
FL uz n_screensize(void);

/* In n_PSO_INTERACTIVE, we want to go over $PAGER.
 * These are specialized version of fs_pipe_open()/fs_pipe_close() which also
 * encapsulate error message printing, terminal handling etc. additionally */
FL FILE *mx_pager_open(void);
FL boole mx_pager_close(FILE *fp);

/* Use a pager or STDOUT to print *fp*; if *lines* is 0, they'll be counted */
FL void        page_or_print(FILE *fp, uz lines);

/* Parse name and guess at the required protocol.
 * If check_stat is true then stat(2) will be consulted - a TODO c..p hack
 * TODO that together with *newfolders*=maildir adds Maildir support; sigh!
 * If try_hooks is set check_stat is implied and if the stat(2) fails all
 * file-hook will be tried in order to find a supported version of name.
 * If adjusted_or_null is not NULL it will be set to the final version of name
 * this function knew about: a %: FEDIT_SYSBOX prefix is forgotten, in case
 * a hook is needed the "real" filename will be placed.
 * TODO This c..p should be URL::from_string()->protocol() or something! */
FL enum protocol  which_protocol(char const *name, boole check_stat,
                     boole try_hooks, char const **adjusted_or_null);

/* Hexadecimal itoa (NUL terminates) / atoi (-1 on error) */
FL char *      n_c_to_hex_base16(char store[3], char c);
FL s32      n_c_from_hex_base16(char const hex[2]);

/* Return the name of the dead.letter file */
FL char const * n_getdeadletter(void);

/* Detect and query the hostname to use */
FL char *n_nodename(boole mayoverride);

/* Convert from / to *ttycharset* */
#ifdef mx_HAVE_IDNA
FL boole n_idna_to_ascii(struct n_string *out, char const *ibuf, uz ilen);
/*TODO FL boole n_idna_from_ascii(struct n_string *out, char const *ibuf,
            uz ilen);*/
#endif

/* Check whether the argument string is a TRU1 or FAL0 boolean, or an invalid
 * string, in which case TRUM1 is returned.
 * If the input buffer is empty emptyrv is used as the return: if it is GE
 * FAL0 it will be made a binary boolean, otherwise TRU2 is returned.
 * inlen may be UZ_MAX to force su_cs_len() detection */
FL boole n_boolify(char const *inbuf, uz inlen, boole emptyrv);

/* Dig a "quadoption" in inbuf, possibly going through getapproval() in
 * interactive mode, in which case prompt is printed first if set.
.  Just like n_boolify() otherwise */
FL boole n_quadify(char const *inbuf, uz inlen, char const *prompt,
            boole emptyrv);

/* Is the argument "all" (case-insensitive) or "*" */
FL boole n_is_all_or_aster(char const *name);

/* Get seconds since epoch, return pointer to static struct.
 * Unless force_update is true we may use the event-loop tick time */
FL struct n_timespec const *n_time_now(boole force_update);
#define n_time_epoch() ((time_t)n_time_now(FAL0)->ts_sec)

/* Update *tc* to now; only .tc_time updated unless *full_update* is true */
FL void        time_current_update(struct time_current *tc,
                  boole full_update);

/* TZ difference in seconds.
 * secsepoch is only used if any of the tm's is NIL. */
FL s32 n_time_tzdiff(s64 secsepoch, struct tm const *utcp_or_nil,
      struct tm const *localp_or_nil);

/* ctime(3), but do ensure 26 byte limit, do not crash XXX static buffer.
 * NOTE: no trailing newline */
FL char *n_time_ctime(s64 secsepoch, struct tm const *localtime_or_nil);

/* Returns 0 if fully slept, number of millis left if ignint is true and we
 * were interrupted.  Actual resolution may be second or less.
 * Note in case of mx_HAVE_SLEEP this may be SIGALARM based. */
FL uz n_msleep(uz millis, boole ignint);

/* Our error print series..  Note: these reverse scan format in order to know
 * whether a newline was included or not -- this affects the output!
 * xxx Prototype changes to be reflected in src/su/core-code. (for now) */
FL void n_err(char const *format, ...);
FL void n_errx(boole allow_multiple, char const *format, ...);
FL void n_verr(char const *format, va_list ap);
FL void n_verrx(boole allow_multiple, char const *format, va_list ap);

/* ..(for use in a signal handler; to be obsoleted..).. */
FL void        n_err_sighdl(char const *format, ...);

/* Our perror(3); if errval is 0 su_err_no() is used; newline appended */
FL void        n_perr(char const *msg, int errval);

/* Announce a fatal error (and die); newline appended */
FL void        n_alert(char const *format, ...);
FL void        n_panic(char const *format, ...);

/* `errors' */
#ifdef mx_HAVE_ERRORS
FL int c_errors(void *vp);
#endif

/* */
#ifdef mx_HAVE_REGEX
FL char const *n_regex_err_to_doc(const regex_t *rep, int e);
#endif

/* Shared code for c_unxy() which base upon su_cs_dict, e.g., `shortcut' */
FL su_boole mx_unxy_dict(char const *cmdname, struct su_cs_dict *dp, void *vp);

/* Sort all keys of dp, iterate over them, call the given hook ptf for each
 * key/data pair, place any non-NIL returned in the *result list.
 * A non-NIL *result will not be updated, but be appended to.
 * tailpp_or_nil can be set to speed up follow runs.
 * The boole return states error, *result may be NIL even upon success,
 * e.g., if dp is NIL or empty */
FL boole mx_xy_dump_dict(char const *cmdname, struct su_cs_dict *dp,
      struct n_strlist **result, struct n_strlist **tailpp_or_nil,
      struct n_strlist *(*ptf)(char const *cmdname, char const *key,
         void const *dat));

/* Default callback which can be used when dat is in fact a char const* */
FL struct n_strlist *mx_xy_dump_dict_gen_ptf(char const *cmdname,
      char const *key, void const *dat);

/* page_or_print() all members of slp, one line per node.
 * If slp is NIL print a line that no cmdname are registered.
 * If cnt_lines is FAL0 then each slp entry is assumed to be one line without
 * a trailing newline character, otherwise these characters are counted and
 * a trailing such is put as necessary */
FL boole mx_page_or_print_strlist(char const *cmdname,
      struct n_strlist *slp, boole cnt_lines);

/*
 * cmd-cnd.c
 */

/* if.elif.else.endif conditional execution */
FL int c_if(void *v);
FL int c_elif(void *v);
FL int c_else(void *v);
FL int c_endif(void *v);

/* Whether an `if' block exists (TRU1) / is in a whiteout condition (TRUM1) */
FL boole n_cnd_if_exists(void);

/* An execution context is teared down, and it finds to have an if stack */
FL void n_cnd_if_stack_del(struct n_go_data_ctx *gdcp);

/*
 * cmd-folder.c
 */

/* `file' (`folder') and `File' (`Folder') */
FL int c_file(void *v);
FL int c_File(void *v);

/* 'newmail' command: Check for new mail without writing old mail back */
FL int c_newmail(void *v);

/* noop */
FL int c_noop(void *v);

/* Remove mailbox */
FL int c_remove(void *v);

/* Rename mailbox */
FL int c_rename(void *v);

/* List the folders the user currently has */
FL int c_folders(void *v);

/*
 * cmd-head.c
 */

/* `headers' (show header group, possibly after setting dot) */
FL int c_headers(void *v);

/* Like c_headers(), but pre-prepared message vector */
FL int print_header_group(int *vector);

/* Scroll to the next/previous screen */
FL int c_scroll(void *v);
FL int c_Scroll(void *v);

/* Move the dot up or down by one message */
FL int c_dotmove(void *v);

/* Print out the headlines for each message in the passed message list */
FL int c_from(void *v);

/* Print all messages in msgvec visible and either only_marked is false or they
 * are MMARKed.
 * TODO If subject_thread_compress is true then a subject will not be printed
 * TODO if it equals the subject of the message "above"; as this only looks
 * TODO in the thread neighbour and NOT in the "visible" neighbour, the caller
 * TODO has to ensure the result will look sane; DROP + make it work (tm) */
FL void print_headers(int const *msgvec, boole only_marked,
         boole subject_thread_compress);

/*
 * cmd-msg.c
 */

/* Paginate messages, honour/don't honour ignored fields, respectively */
FL int c_more(void *v);
FL int c_More(void *v);

/* Type out messages, honour/don't honour ignored fields, respectively */
FL int c_type(void *v);
FL int c_Type(void *v);

/* Show raw message content */
FL int c_show(void *v);

/* `mimeview' */
FL int c_mimeview(void *vp);

/* Pipe messages, honour/don't honour ignored fields, respectively */
FL int c_pipe(void *vp);
FL int c_Pipe(void *vp);

/* Print the first *toplines* of each desired message */
FL int c_top(void *v);
FL int c_Top(void *v);

/* If any arguments were given, go to the next applicable argument following
 * dot, otherwise, go to the next applicable message.  If given as first
 * command with no arguments, print first message */
FL int c_next(void *v);

/* `=': print out the value(s) of <msglist> (or dot) */
FL int c_pdot(void *vp);

/* Print the size of each message */
FL int c_messize(void *v);

/* Delete messages */
FL int c_delete(void *v);

/* Delete messages, then type the new dot */
FL int c_deltype(void *v);

/* Undelete the indicated messages */
FL int c_undelete(void *v);

/* Touch all the given messages so that they will get mboxed */
FL int c_stouch(void *v);

/* Make sure all passed messages get mboxed */
FL int c_mboxit(void *v);

/* Preserve messages, so that they will be sent back to the system mailbox */
FL int c_preserve(void *v);

/* Mark all given messages as unread */
FL int c_unread(void *v);

/* Mark all given messages as read */
FL int c_seen(void *v);

/* Message flag manipulation */
FL int c_flag(void *v);
FL int c_unflag(void *v);
FL int c_answered(void *v);
FL int c_unanswered(void *v);
FL int c_draft(void *v);
FL int c_undraft(void *v);

/*
 * cmd-misc.c
 */

/* `!': process a shell escape by saving signals, ignoring signals and sh -c */
FL int c_shell(void *v);

/* `shell': fork an interactive shell */
FL int c_dosh(void *v);

/* `cwd': print user's working directory */
FL int c_cwd(void *v);

/* `chdir': change user's working directory */
FL int c_chdir(void *v);

/* `echo' series: expand file names like echo (to stdout/stderr, with/out
 * trailing newline) */
FL int c_echo(void *v);
FL int c_echoerr(void *v);
FL int c_echon(void *v);
FL int c_echoerrn(void *v);

/* `read', `readsh' */
FL int c_read(void *vp);
FL int c_readsh(void *vp);

/* `readall' */
FL int c_readall(void *vp);

/* `version', and generic support for the shared initial version line, which
 * appends to sp the UA name, version etc., and a \n LF */
FL struct n_string *n_version(struct n_string *sp);
FL int c_version(void *vp);

/*
 * cmd-resend.c
 */

/* All thinkable sorts of `reply' / `respond' and `followup'.. */
FL int c_reply(void *vp);
FL int c_replyall(void *vp); /* v15-compat */
FL int c_replysender(void *vp); /* v15-compat */
FL int c_Reply(void *vp);
FL int c_followup(void *vp);
FL int c_followupall(void *vp); /* v15-compat */
FL int c_followupsender(void *vp); /* v15-compat */
FL int c_Followup(void *vp);

/* ..and a mailing-list reply and followup */
FL int c_Lreply(void *vp);
FL int c_Lfollowup(void *vp);

/* 'forward' / `Forward' */
FL int c_forward(void *vp);
FL int c_Forward(void *vp);

/* Resend a message list to a third person.
 * The latter does not add the Resent-* header series */
FL int c_resend(void *vp);
FL int c_Resend(void *vp);

/*
 * cmd-write.c
 */

/* Save a message in a file.  Mark the message as saved so we can discard when
 * the user quits */
FL int c_save(void *vp);
FL int c_Save(void *vp);

/* Copy a message to a file without affected its saved-ness */
FL int c_copy(void *vp);
FL int c_Copy(void *vp);

/* Move a message to a file */
FL int c_move(void *vp);
FL int c_Move(void *vp);

/* Decrypt and copy a message to a file.  Like plain `copy' at times */
FL int c_decrypt(void *vp);
FL int c_Decrypt(void *vp);

/* Write the indicated messages at the end of the passed file name, minus
 * header and trailing blank line.  This is the MIME save function */
FL int c_write(void *vp);

/*
 * collect.c
 */

/* temporary_compose_mode_hook_call() etc. setter hook */
FL void n_temporary_compose_hook_varset(void *arg);

/* If quotefile is (char*)-1, stdin will be used, caller has to verify that
 * we're not running in interactive mode */
FL FILE *n_collect(enum n_mailsend_flags msf, struct header *hp,
            struct message *mp, char const *quotefile, s8 *checkaddr_err);

/*
 * folder.c
 */

/* Set up editing on the given file name.
 * If the first character of name is %, we are considered to be editing the
 * file, otherwise we are reading our mail which has signficance for mbox and
 * so forth */
FL int         setfile(char const *name, enum fedit_mode fm);

FL int         newmailinfo(int omsgCount);

/* Set the size of the message vector used to construct argument lists to
 * message list functions */
FL void        setmsize(int sz);

/* Logic behind -H / -L invocations */
FL void        print_header_summary(char const *Larg);

/* Announces the current folder as indicated.
 * Is responsible for updating "dot" (after a folder change). */
FL void n_folder_announce(enum n_announce_flags af);

FL int         getmdot(int nmail);

FL void        initbox(char const *name);

/* Determine and expand the current *folder* name, return it (with trailing
 * solidus) or the empty string also in case of errors: since POSIX mandates
 * a default of CWD if not set etc., that seems to be a valid fallback, then */
FL char const *n_folder_query(void);

/* Prepare the seekable O_APPEND MBOX fout for appending of another message.
 * If st_or_null is not NULL it is assumed to point to an up-to-date status of
 * fout, otherwise an internal fstat(2) is performed as necessary.
 * Returns su_err_no() of error */
FL int n_folder_mbox_prepare_append(FILE *fout, struct stat *st_or_null);

/*
 * go.c
 * Program input of all sorts, input lexing, event loops, command evaluation.
 * Also alias handling.
 */

/* Setup the run environment; this i *only* for main() */
FL void n_go_init(void);

/* Interpret user commands.  If stdin is not a tty, print no prompt; return
 * whether last processed command returned error; this is *only* for main()! */
FL boole n_go_main_loop(void);

/* Actual cmd input */

/* */
FL void n_go_input_clearerr(void);

/* Force n_go_input() to read EOF next */
FL void n_go_input_force_eof(void);

/* Returns true if force_eof() has been set -- it is set automatically if
 * an input context enters EOF state (rather than error, as in ferror(3)) */
FL boole n_go_input_is_eof(void);

/* Are there any go_input_inject()ions pending? */
FL boole n_go_input_have_injections(void);

/* Force n_go_input() to read that buffer next.
 * If n_GO_INPUT_INJECT_COMMIT is not set the line editor is reentered with buf
 * as the default/initial line content */
FL void n_go_input_inject(enum n_go_input_inject_flags giif, char const *buf,
            uz len);

/* Read a complete line of input, with editing if interactive and possible.
 * string_or_nil is the optional initial line content if in interactive
 * mode, otherwise this argument is ignored for reproducibility.
 * If histok_or_nil is set it will be updated to FAL0 if input shall not be
 * placed in history.
 * Return number of octets or a value <0 on error.
 * Note: may use the currently `source'd file stream instead of stdin!
 * Manages the n_PS_READLINE_NL hack
 * TODO We need an OnReadLineCompletedEvent and drop this function */
FL int n_go_input(enum n_go_input_flags gif, char const *prompt_or_nil,
         char **linebuf, uz *linesize, char const *string_or_nil,
         boole *histok_or_nil  su_DBG_LOC_ARGS_DECL);
#ifdef su_HAVE_DBG_LOC_ARGS
# define n_go_input(A,B,C,D,E,F) n_go_input(A,B,C,D,E,F  su_DBG_LOC_ARGS_INJ)
#endif

/* Like go_input(), but return savestr()d result or NIL in case of errors or if
 * an empty line would be returned.
 * This may only be called from toplevel (not during n_PS_ROBOT) */
FL char *n_go_input_cp(enum n_go_input_flags gif, char const *prompt_or_nil,
            char const *string_or_nil);

/* Deal with loading of resource files and dealing with a stack of files for
 * the source command */

/* Load a file of user system startup resources.
 * *Only* for main(), returns whether program shall continue */
FL boole n_go_load_rc(char const *name);

/* "Load" or go_inject() command line option "cmd" arguments in order.
 * *Only* for main(), returns whether program shall continue unless injectit is
 * set, in which case this function does not fail.
 * If lines is NIL the builtin RC file is used, and errors are ignored */
FL boole n_go_load_lines(boole injectit, char const **lines, uz cnt);

/* Pushdown current input file and switch to a new one. */
FL int c_source(void *v);
FL int c_source_if(void *v);

/* Evaluate a complete macro / a single command.  For the former lines will
 * be free()d, for the latter cmd will always be duplicated internally */
FL boole n_go_macro(enum n_go_input_flags gif, char const *name, char **lines,
            void (*on_finalize)(void*), void *finalize_arg);
FL boole n_go_command(enum n_go_input_flags gif, char const *cmd);

/* XXX See a_GO_SPLICE in source */
FL void n_go_splice_hack(char const *cmd, FILE *new_stdin, FILE *new_stdout,
         u32 new_psonce, void (*on_finalize)(void*), void *finalize_arg);
FL void n_go_splice_hack_remove_after_jump(void);

/* XXX Hack: may we release our (interactive) (terminal) control to a different
 * XXX program, e.g., a $PAGER? */
FL boole n_go_may_yield_control(void);

/* `eval' */
FL int c_eval(void *vp);

/* `xcall' */
FL int c_xcall(void *vp);

/* `exit' and `quit' commands */
FL int c_exit(void *vp);
FL int c_quit(void *vp);

/* `readctl' */
FL int c_readctl(void *vp);

/*
 * header.c
 */

/* Return the user's From: address(es) */
FL char const * myaddrs(struct header *hp);

/* Boil the user's From: addresses down to a single one, or use *sender* */
FL char const * myorigin(struct header *hp);

/* See if the passed line buffer, which may include trailing newline (sequence)
 * is a mail From_ header line according to POSIX ("From ").
 * If check_rfc4155 is true we'll return TRUM1 instead if the From_ line
 * matches POSIX but is _not_ compatible to RFC 4155 */
FL boole      is_head(char const *linebuf, uz linelen,
                  boole check_rfc4155);

/* Return pointer to first non-header, non-space character, or NIL if invalid.
 * If lead_ws is true leading whitespace is allowed and skipped.
 * If cramp_or_nil is not NIL it will be set to the valid header name itself */
FL char const *mx_header_is_valid(char const *name, boole lead_ws,
      struct str *cramp_or_nil);

/* Print hp "to user interface" fp for composing purposes xxx what a sigh */
FL boole n_header_put4compose(FILE *fp, struct header *hp);

/* Extract some header fields (see e.g. -t documentation) from a message.
 * This calls expandaddr() on some headers and sets checkaddr_err_or_null if
 * that is set -- note it explicitly allows EAF_NAME because aliases are not
 * expanded when this is called! */
FL void n_header_extract(enum n_header_extract_flags hef, FILE *fp,
         struct header *hp, s8 *checkaddr_err_or_null);

/* Return the desired header line from the passed message
 * pointer (or NULL if the desired header field is not available).
 * If mult is zero, return the content of the first matching header
 * field only, the content of all matching header fields else */
FL char *      hfield_mult(char const *field, struct message *mp, int mult);
#define hfieldX(a, b)            hfield_mult(a, b, 1)
#define hfield1(a, b)            hfield_mult(a, b, 0)

/* Check whether the passed line is a header line of the desired breed.
 * If qm_suffix_or_nil is set then the field?[MOD]: syntax is supported, the
 * suffix substring range of linebuf will be stored in there, then, or NIL;
 * this logically casts away the const.
 * Return the field body, or NULL */
FL char const *n_header_get_field(char const *linebuf, char const *field,
      struct str *qm_suffix_or_nil);

/* Start of a "comment".  Ignore it */
FL char const * skip_comment(char const *cp);

/* Return the start of a route-addr (address in angle brackets), if present */
FL char const * routeaddr(char const *name);

/* Query *expandaddr*, parse it and return flags.
 * Flags are already adjusted for n_PSO_INTERACTIVE, n_PO_TILDE_FLAG etc. */
FL enum expand_addr_flags expandaddr_to_eaf(void);

/* Check if an address is invalid, either because it is malformed or, if not,
 * according to eacm.  Return FAL0 when it looks good, TRU1 if it is invalid
 * but the error condition was not covered by a 'hard "fail"ure', else -1 */
FL s8       is_addr_invalid(struct mx_name *np,
                  enum expand_addr_check_mode eacm);

/* Does *NP* point to a file or pipe addressee? */
#define is_fileorpipe_addr(NP)   \
   (((NP)->n_flags & mx_NAME_ADDRSPEC_ISFILEORPIPE) != 0)

/* Skin an address according to the RFC 822 interpretation of "host-phrase" */
FL char *      skin(char const *name);

/* Skin *name* and extract *addr-spec* according to RFC 5322 and enum gfield.
 * Store the result in .ag_skinned and also fill in those .ag_ fields that have
 * actually been seen.
 * Return NULL on error, or name again, but which may have been replaced by
 * a version with fixed quotation etc.! */
FL char const *n_addrspec_with_guts(struct n_addrguts *agp, char const *name,
      u32 gfield);

/* `addrcodec' */
FL int c_addrcodec(void *vp);

/* Fetch the real name from an internet mail address field */
FL char *      realname(char const *name);

/* Look for a RFC 2369 List-Post: header, return NIL if none was found, -1 if
 * the one found forbids posting to the list, a header otherwise.
 * .n_type needs to be set to something desired still */
FL struct mx_name *mx_header_list_post_of(struct message *mp);

/* Get the sender (From: or Sender:) of this message, or NIL.
 * If gf is 0 GFULL|GSKIN is used (no senderfield beside that) */
FL struct mx_name *mx_header_sender_of(struct message *mp, u32 gf);

/* Get header_sender_of(), or From_ line from this message.
 * The return value may be empty and needs lextract()ion */
FL char *n_header_senderfield_of(struct message *mp);

/* Trim away all leading Re: etc., return pointer to plain subject.
 * Note it doesn't perform any MIME decoding by itself */
FL char const *subject_re_trim(char const *cp);

FL int         msgidcmp(char const *s1, char const *s2);

/* Fake Sender for From_ lines if missing, e. g. with POP3 */
FL char const * fakefrom(struct message *mp);

/* From username Fri Jan  2 20:13:51 2004
 *               |    |    |    |    |
 *               0    5   10   15   20 */
#if defined mx_HAVE_IMAP_SEARCH || defined mx_HAVE_IMAP
FL time_t      unixtime(char const *from);
#endif

FL time_t      rfctime(char const *date);

FL time_t      combinetime(int year, int month, int day,
                  int hour, int minute, int second);

/* Determine the date to print in faked 'From ' lines */
FL void        substdate(struct message *m);

/* Create ready-to-go environment taking into account *datefield* etc.,
 * and return a result in auto-reclaimed storage.
 * TODO hack *color_tag_or_null could be set to n_COLOUR_TAG_SUM_OLDER.
 * time_current is used for comparison and must thus be up-to-date */
FL char *n_header_textual_date_info(struct message *mp,
            char const **color_tag_or_null);

/* Create ready-to-go sender name of a message in *cumulation_or_null, the
 * addresses only in *addr_or_null, the real names only in *name_real_or_null,
 * and the full names in *name_full_or_null, taking account for *showname*.
 * If *is_to_or_null is set, *showto* and n_is_myname() are taken into account
 * when choosing which names to use.
 * The list as such is returned, or NULL if there is really none (empty strings
 * will be stored, then).
 * All results are in auto-reclaimed storage, but may point to the same string.
 * TODO *is_to_or_null could be set to whether we actually used To:, or not.
 * TODO n_header_textual_sender_info(): should only create a list of matching
 * TODO name objects, which the user can iterate over and o->to_str().. */
FL struct mx_name *n_header_textual_sender_info(struct message *mp,
                  char **cumulation_or_null, char **addr_or_null,
                  char **name_real_or_null, char **name_full_or_null,
                  boole *is_to_or_null);

/* TODO Weird thing that tries to fill in From: and Sender: */
FL void        setup_from_and_sender(struct header *hp);

/* Note: returns 0x1 if both args were NULL */
FL struct mx_name const *check_from_and_sender(struct mx_name const *fromfield,
                        struct mx_name const *senderfield);

#ifdef mx_HAVE_XTLS
FL char *      getsender(struct message *m);
#endif

/* This returns NULL if hp is NULL or when no information is available.
 * hp remains unchanged (->h_in_reply_to is not set!)  */
FL struct mx_name *n_header_setup_in_reply_to(struct header *hp);

/* Fill in / reedit the desired header fields */
FL int         grab_headers(enum n_go_input_flags gif, struct header *hp,
                  enum gfield gflags, int subjfirst);

/* Check whether sep->ss_sexpr (or ->ss_sregex) matches any header of mp.
 * If sep->s_where (or >s_where_wregex) is set, restrict to given headers */
FL boole n_header_match(struct message *mp, struct search_expr const *sep);

/* Verify whether len (UZ_MAX=su_cs_len) bytes of name form a standard or
 * otherwise known header name (that must not be used as a custom header).
 * Return the (standard) header name, or NULL */
FL char const *n_header_is_known(char const *name, uz len);

/* Add a custom header to the given list, in auto-reclaimed or heap memory;
 * if heap is TRUM1 then only check state; hflp can be NIL, then! */
FL boole n_header_add_custom(struct n_header_field **hflp, char const *dat,
            boole heap);

/*
 * ignore.c
 */

/* `(un)?headerpick' */
FL int c_headerpick(void *vp);
FL int c_unheaderpick(void *vp);

/* TODO Compat variants of the c_(un)?h*() series,
 * except for `retain' and `ignore', which are standardized */
FL int c_retain(void *vp);
FL int c_ignore(void *vp);
FL int c_unretain(void *vp);
FL int c_unignore(void *vp);

FL int         c_saveretain(void *v);
FL int         c_saveignore(void *v);
FL int         c_unsaveretain(void *v);
FL int         c_unsaveignore(void *v);

FL int         c_fwdretain(void *v);
FL int         c_fwdignore(void *v);
FL int         c_unfwdretain(void *v);
FL int         c_unfwdignore(void *v);

/* Ignore object lifecycle.  (Most of the time this interface deals with
 * special n_IGNORE_* objects, e.g., n_IGNORE_TYPE, though.)
 * isauto: whether auto-reclaimed storage is to be used for allocations;
 * if so, _del() needn't be called */
FL struct n_ignore *n_ignore_new(boole isauto);
FL void n_ignore_del(struct n_ignore *self);

/* Are there just _any_ user settings covered by self? */
FL boole n_ignore_is_any(struct n_ignore const *self);

/* Set an entry to retain (or ignore).
 * Returns FAL0 if dat is not a valid header field name or an invalid regular
 * expression, TRU1 if insertion took place, and TRUM1 if already set */
FL boole n_ignore_insert(struct n_ignore *self, boole retain,
            char const *dat, uz len);
#define n_ignore_insert_cp(SELF,RT,CP) n_ignore_insert(SELF, RT, CP, UZ_MAX)

/* Returns TRU1 if retained, TRUM1 if ignored, FAL0 if not covered */
FL boole n_ignore_lookup(struct n_ignore const *self, char const *dat,
            uz len);
#define n_ignore_lookup_cp(SELF,CP) n_ignore_lookup(SELF, CP, UZ_MAX)
#define n_ignore_is_ign(SELF,FDAT,FLEN) \
   (n_ignore_lookup(SELF, FDAT, FLEN) == TRUM1)

/*
 * imap-search.c
 */

/* Return -1 on invalid spec etc., the number of matches otherwise */
#ifdef mx_HAVE_IMAP_SEARCH
FL sz     imap_search(char const *spec, int f);
#endif

/*
 * maildir.c
 */

#ifdef mx_HAVE_MAILDIR
FL int maildir_setfile(char const *who, char const *name, enum fedit_mode fm);

FL boole maildir_quit(boole hold_sigs_on);

FL enum okay maildir_append(char const *name, FILE *fp, long offset);

FL enum okay maildir_remove(char const *name);
#endif /* mx_HAVE_MAILDIR */

/*
 * (Former memory.c, now SU TODO get rid of compat macros)
 * Heap memory and automatically reclaimed storage, plus pseudo "alloca"
 *
 */

/* Generic heap memory */
#define n_alloc su_MEM_ALLOC
#define n_realloc su_MEM_REALLOC
#define n_calloc(NO,SZ) su_MEM_CALLOC_N(SZ, NO)
#define n_free su_MEM_FREE

/* Auto-reclaimed storage */
#define n_autorec_relax_create() \
      su_mem_bag_auto_relax_create(n_go_data->gdc_membag)
#define n_autorec_relax_gut() \
      su_mem_bag_auto_relax_gut(n_go_data->gdc_membag)
#define n_autorec_relax_unroll() \
      su_mem_bag_auto_relax_unroll(n_go_data->gdc_membag)
/* (Even older obsolete names!) */
#define srelax_hold n_autorec_relax_create
#define srelax_rele n_autorec_relax_gut
#define srelax n_autorec_relax_unroll

#define n_autorec_alloc su_MEM_BAG_SELF_AUTO_ALLOC
#define n_autorec_calloc(NO,SZ) su_MEM_BAG_SELF_AUTO_CALLOC_N(SZ, NO)

/* Pseudo alloca (and also auto-reclaimed) */
#define n_lofi_alloc su_MEM_BAG_SELF_LOFI_ALLOC
#define n_lofi_calloc su_MEM_BAG_SELF_LOFI_CALLOC
#define n_lofi_free su_MEM_BAG_SELF_LOFI_FREE

#define n_lofi_snap_create() su_mem_bag_lofi_snap_create(n_go_data->gdc_membag)
#define n_lofi_snap_unroll(COOKIE) \
   su_mem_bag_lofi_snap_unroll(n_go_data->gdc_membag, COOKIE)

/*
 * message.c
 */

/* Return a file buffer all ready to read up the passed message pointer */
FL FILE *      setinput(struct mailbox *mp, struct message *m,
                  enum needspec need);

/*  */
FL enum okay   get_body(struct message *mp);

/* Reset (free) the global message array */
FL void        message_reset(void);

/* Append the passed message descriptor onto the message array; if mp is NULL,
 * NULLify the entry at &[msgCount-1] */
FL void        message_append(struct message *mp);

/* Append a NULL message */
FL void        message_append_null(void);

/* Check whether sep->ss_sexpr (or ->ss_sregex) matches mp.  If with_headers is
 * true then the headers will also be searched (as plain text) */
FL boole      message_match(struct message *mp, struct search_expr const *sep,
               boole with_headers);

/*  */
FL struct message * setdot(struct message *mp);

/* Touch the named message by setting its MTOUCH flag.  Touched messages have
 * the effect of not being sent back to the system mailbox on exit */
FL void        touch(struct message *mp);

/* Convert user message spec. to message numbers and store them in vector,
 * which should be capable to hold msgCount+1 entries (n_msgvec ASSERTs this).
 * flags is cmd_arg_ctx.cac_msgflag==cmd_desc.cd_mflags_o_minargs==enum mflag.
 * If capp_or_null is not NULL then the last (string) token is stored in here
 * and not interpreted as a message specification; in addition, if only one
 * argument remains and this is the empty string, 0 is returned (*vector=0;
 * this is used to implement CMD_ARG_DESC_MSGLIST_AND_TARGET).
 * A NUL *buf input results in a 0 return, *vector=0, [*capp_or_null=NULL].
 * Returns the count of messages picked up or -1 on error */
FL int n_getmsglist(char const *buf, int *vector, int flags,
         struct mx_cmd_arg **capp_or_null);

/* Find the first message whose flags&m==f and return its message number */
FL int         first(int f, int m);

/* Mark the named message by setting its mark bit */
FL void        mark(int mesg, int f);

/*
 * mime.c
 */

/* *sendcharsets* .. *charset-8bit* iterator; *a_charset_to_try_first* may be
 * used to prepend a charset to this list (e.g., for *reply-in-same-charset*).
 * The returned boolean indicates charset_iter_is_valid().
 * Without mx_HAVE_ICONV, this "iterates" over *ttycharset* only */
FL boole      charset_iter_reset(char const *a_charset_to_try_first);
FL boole      charset_iter_next(void);
FL boole      charset_iter_is_valid(void);
FL char const * charset_iter(void);

/* And this is (xxx temporary?) which returns the iterator if that is valid and
 * otherwise either *charset-8bit* or *ttycharset*, dep. on mx_HAVE_ICONV */
FL char const * charset_iter_or_fallback(void);

FL void        charset_iter_recurse(char *outer_storage[2]); /* TODO LEGACY */
FL void        charset_iter_restore(char *outer_storage[2]); /* TODO LEGACY */

/* Check whether our headers will need MIME conversion */
#ifdef mx_HAVE_ICONV
FL char const * need_hdrconv(struct header *hp);
#endif

/* Convert header fields from RFC 1522 format */
FL void        mime_fromhdr(struct str const *in, struct str *out,
                  enum tdflags flags);

/* Interpret MIME strings in parts of an address field */
FL char *      mime_fromaddr(char const *name);

/* fwrite(3) performing the given MIME conversion */
FL sz     mime_write(char const *ptr, uz size, FILE *f,
                  enum conversion convert, enum tdflags dflags,
                  struct quoteflt *qf, struct str *outrest,
                  struct str *inrest);
FL sz     xmime_write(char const *ptr, uz size, /* TODO LEGACY */
                  FILE *f, enum conversion convert, enum tdflags dflags,
                  struct str *outrest, struct str *inrest);

/*
 * mime-enc.c
 * Content-Transfer-Encodings as defined in RFC 2045 (and RFC 2047):
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8
 * TODO For now this is pretty mixed up regarding this external interface
 * TODO (and due to that the code is, too).
 * TODO In v15.0 CTE will be filter based, and explicit conversion will
 * TODO gain clear error codes
 */

/* Default MIME Content-Transfer-Encoding: as via *mime-encoding*.
 * Cannot be MIMEE_BIN nor MIMEE_7B (i.e., only B64, QP, 8B) */
FL enum mime_enc mime_enc_target(void);

/* Map from a Content-Transfer-Encoding: header body (which may be NULL) */
FL enum mime_enc mime_enc_from_ctehead(char const *hbody);

/* XXX Try to get rid of that */
FL char const * mime_enc_from_conversion(enum conversion const convert);

/* How many characters of (the complete body) ln need to be quoted.
 * Only MIMEEF_ISHEAD and MIMEEF_ISENCWORD are understood */
FL uz      mime_enc_mustquote(char const *ln, uz lnlen,
                  enum mime_enc_flags flags);

/* How much space is necessary to encode len bytes in QP, worst case.
 * Includes room for terminator, UZ_MAX on overflow */
FL uz      qp_encode_calc_size(uz len);

/* If flags includes QP_ISHEAD these assume "word" input and use special
 * quoting rules in addition; soft line breaks are not generated.
 * Otherwise complete input lines are assumed and soft line breaks are
 * generated as necessary.  Return NULL on error (overflow) */
FL struct str * qp_encode(struct str *out, struct str const *in,
                  enum qpflags flags);
#ifdef notyet
FL struct str * qp_encode_cp(struct str *out, char const *cp,
                  enum qpflags flags);
FL struct str * qp_encode_buf(struct str *out, void const *vp, uz vp_len,
                  enum qpflags flags);
#endif

/* The buffers of out and *rest* will be managed via n_realloc().
 * If inrest_or_null is needed but NULL an error occurs, otherwise tolerant.
 * Return FAL0 on error; caller is responsible to free buffers */
FL boole      qp_decode_header(struct str *out, struct str const *in);
FL boole      qp_decode_part(struct str *out, struct str const *in,
                  struct str *outrest, struct str *inrest_or_null);

/* How much space is necessary to encode len bytes in Base64, worst case.
 * Includes room for (CR/LF/CRLF and) terminator, UZ_MAX on overflow */
FL uz      b64_encode_calc_size(uz len);

/* Note these simply convert all the input (if possible), including the
 * insertion of NL sequences if B64_CRLF or B64_LF is set (and multiple thereof
 * if B64_MULTILINE is set).
 * Thus, in the B64_BUF case, better call b64_encode_calc_size() first.
 * Return NULL on error (overflow; cannot happen for B64_BUF) */
FL struct str * b64_encode(struct str *out, struct str const *in,
                  enum b64flags flags);
FL struct str * b64_encode_buf(struct str *out, void const *vp, uz vp_len,
                  enum b64flags flags);
#ifdef notyet
FL struct str * b64_encode_cp(struct str *out, char const *cp,
                  enum b64flags flags);
#endif

/* The _{header,part}() variants are failure tolerant, the latter requires
 * outrest to be set; due to the odd 4:3 relation inrest_or_null should be
 * given, _then_, it is an error if it is needed but not set.
 * TODO pre v15 callers should ensure that no endless loop is entered because
 * TODO the inrest cannot be converted and ends up as inrest over and over:
 * TODO give NULL to stop such loops.
 * The buffers of out and possibly *rest* will be managed via n_realloc().
 * Returns FAL0 on error; caller is responsible to free buffers.
 * XXX FAL0 is effectively not returned for _part*() variants,
 * XXX (instead replacement characters are produced for invalid data.
 * XXX _Unless_ operation could EOVERFLOW.)
 * XXX I.e. this is bad and is tolerant for text and otherwise not */
FL boole      b64_decode(struct str *out, struct str const *in);
FL boole      b64_decode_header(struct str *out, struct str const *in);
FL boole      b64_decode_part(struct str *out, struct str const *in,
                  struct str *outrest, struct str *inrest_or_null);

/*
 * mime-param.c
 */

/* Get a mime style parameter from a header body */
FL char *      mime_param_get(char const *param, char const *headerbody);

/* Format parameter name to have value, autorec_alloc() it or NULL in result.
 * 0 on error, 1 or -1 on success: the latter if result contains \n newlines,
 * which it will if the created param requires more than MIME_LINELEN bytes;
 * there is never a trailing newline character */
/* TODO mime_param_create() should return a StrList<> or something.
 * TODO in fact it should take a HeaderField* and append HeaderFieldParam*! */
FL s8       mime_param_create(struct str *result, char const *name,
                  char const *value);

/* Get the boundary out of a Content-Type: multipart/xyz header field, return
 * autorec_alloc()ed copy of it; store su_cs_len() in *len if set */
FL char *      mime_param_boundary_get(char const *headerbody, uz *len);

/* Create a autorec_alloc()ed MIME boundary */
FL char *      mime_param_boundary_create(void);

/*
 * mime-parse.c
 */

/* Create MIME part object tree for and of mp */
FL struct mimepart * mime_parse_msg(struct message *mp,
                        enum mime_parse_flags mpf);

/*
 * path.c
 */

/* Test to see if the passed file name is a directory, return true if it is.
 * If check_access is set, we also access(2): if it is TRUM1 only X_OK|R_OK is
 * tested, otherwise X_OK|R_OK|W_OK. */
FL boole n_is_dir(char const *name, boole check_access);

/* Recursively create a directory */
FL boole n_path_mkdir(char const *name);

/* Delete a file, but only if the file is a plain file; return FAL0 on system
 * error and TRUM1 if name is not a plain file, return TRU1 on success */
FL boole n_path_rm(char const *name);

/* A get-wd..restore-wd approach */
FL enum okay   cwget(struct cw *cw);
FL enum okay   cwret(struct cw *cw);
FL void        cwrelse(struct cw *cw);

/*
 * quit.c
 */

/* Save all of the undetermined messages at the top of "mbox".  Save all
 * untouched messages back in the system mailbox.  Remove the system mailbox,
 * if none saved there.
 * TODO v15 Note: assumes hold_sigs() has been called _and_ can be temporarily
 * TODO dropped via a single rele_sigs() if hold_sigs_on */
FL boole      quit(boole hold_sigs_on);

/* Adjust the message flags in each message */
FL int         holdbits(void);

/* Create another temporary file and copy user's mbox file darin.  If there is
 * no mbox, copy nothing.  If he has specified "append" don't copy his mailbox,
 * just copy saveable entries at the end */
FL enum okay   makembox(void);

FL void        save_mbox_for_possible_quitstuff(void); /* TODO DROP IF U CAN */

FL int         savequitflags(void);

FL void        restorequitflags(int);

/*
 * send.c
 */

/* Send message described by the passed pointer to the passed output buffer.
 * Return -1 on error.  Adjust the status: field if need be.  If doitp is
 * given, suppress ignored header fields.  prefix is a string to prepend to
 * each output line.   action = data destination
 * (SEND_MBOX,_TOFILE,_TODISP,_QUOTE,_DECRYPT).  stats[0] is line count,
 * stats[1] is character count.  stats may be NULL.  Note that stats[0] is
 * valid for SEND_MBOX only */
FL int         sendmp(struct message *mp, FILE *obuf,
                  struct n_ignore const *doitp,
                  char const *prefix, enum sendaction action, u64 *stats);

/*
 * sendout.c
 */

/* Check whether outgoing transport is via SMTP/SUBMISSION etc.
 * It handles all the *mta* (v15-compat: +*smtp*) cases.
 * Returns TRU1 if yes and URL parsing succeeded, TRUM1 if *mta* is file:// or
 * test:// based, and FAL0 on failure.
 * TODO It will assign CPROTO_NONE and only set urlp->url_input for file-based
 * TODO and test protos, .url_portno is 0 for the former, U16_MAX for latter.
 * TODO Should simply leave all that up to URL, is URL_PROTO_FILExy then). */
FL boole mx_sendout_mta_url(struct mx_url *urlp);

/* For main() only: interface between the command line argument list and the
 * mail1 routine which does all the dirty work */
FL int n_mail(enum n_mailsend_flags msf, struct mx_name *to,
      struct mx_name *cc, struct mx_name *bcc, char const *subject,
      struct mx_attachment *attach, char const *quotefile);

/* `mail' and `Mail' commands, respectively */
FL int c_sendmail(void *v);
FL int c_Sendmail(void *v);

/* Mail a message on standard input to the people indicated in the passed
 * header, applying all the address massages first.  (Internal interface) */
FL enum okay n_mail1(enum n_mailsend_flags flags, struct header *hp,
               struct message *quote, char const *quotefile);

/* Create a Date: header field.
 * We compare the localtime() and gmtime() results to get the timezone, because
 * numeric timezones are easier to read and because $TZ isn't always set.
 * Return number of bytes written of -1 */
FL int mkdate(FILE *fo, char const *field);

/* Dump the to, subject, cc header on the passed file buffer.
 * nosend_msg tells us not to dig to deep but to instead go for compose mode or
 * editing a message (yet we're stupid and cannot do it any better) - if it is
 * TRUM1 then we're really in compose mode and will produce some fields for
 * easier filling in (see n_run_editor() proto for this hack) */
FL boole n_puthead(boole nosend_msg, struct header *hp, FILE *fo,
                  enum gfield w, enum sendaction action,
                  enum conversion convert, char const *contenttype,
                  char const *charset);

/* Note: hp->h_to must already have undergone address massage(s), it is taken
 * as-is; h_cc and h_bcc are asserted to be NIL.  urlp must have undergone
 * mx_sendout_mta_url() processing */
FL enum okay n_resend_msg(struct message *mp, struct mx_url *urlp,
      struct header *hp, boole add_resent);

/* *save* / $DEAD */
FL void        savedeadletter(FILE *fp, boole fflush_rewind_first);

/*
 * shexp.c
 */

/* Evaluate the string given as a new mailbox name. Supported meta characters:
 * . %  for my system mail box
 * . %user for user's system mail box
 * . #  for previous file
 * . &  invoker's mbox file
 * . +file file in folder directory
 * . any shell meta character (except for FEXP_NSHELL).
 * a poor man's vis(3), on name before calling this (and showing the user).
 * If FEXP_MULTIOK is set we return an array of terminated strings, the (last)
 * result string is terminated via \0\0 and n_PS_EXPAND_MULTIRESULT is set.
 * Returns the file name as an auto-reclaimed string */
FL char *fexpand(char const *name, BITENUM_IS(u32,fexp_mode) fexpm);

/* Parse the next shell token from input (->s and ->l are adjusted to the
 * remains, data is constant beside that; ->s may be NULL if ->l is 0, if ->l
 * EQ UZ_MAX su_cs_len(->s) is used) and append the resulting output to store.
 * If cookie is not NULL and we're in double-quotes then ${@} will be exploded
 * just as known from the sh(1)ell in that case */
FL BITENUM_IS(u32,n_shexp_state) n_shexp_parse_token(
      BITENUM_IS(u32,n_shexp_parse_flags) flags, struct n_string *store,
      struct str *input, void const **cookie);

/* Quick+dirty simplified : if an error occurs, returns a copy of *cp and set
 * *cp to NULL, otherwise advances *cp to over the parsed token */
FL char *n_shexp_parse_token_cp(BITENUM_IS(u32,n_shexp_parse_flags) flags,
      char const **cp);

/* Another variant of parse_token_cp(): unquote the argument, ensure the result
 * is "alone": after WS/IFS trimming STATE_STOP must be set, returns TRUM1 if
 * not, TRU1 if STATE_OUTPUT is set, TRU2 if not, FAL0 on error */
FL boole n_shexp_unquote_one(struct n_string *store, char const *input);

/* Quote input in a way that can, in theory, be fed into parse_token() again.
 * ->s may be NULL if ->l is 0, if ->l EQ UZ_MAX su_cs_len(->s) is used.
 * If rndtrip is true we try to make the resulting string "portable" (by
 * converting Unicode to \u etc.), otherwise we produce something to be
 * consumed "now", i.e., to display for the user.
 * Resulting output is _appended_ to store.
 * TODO Note: last resort, since \u and $ expansions etc. are necessarily
 * TODO already expanded and can thus not be reverted, but ALL we have */
FL struct n_string *n_shexp_quote(struct n_string *store,
                     struct str const *input, boole rndtrip);
FL char *n_shexp_quote_cp(char const *cp, boole rndtrip);

/* Can name be used as a variable name (for the process environment)?
 * I.e., this returns false for special parameter names like $# etc. */
FL boole n_shexp_is_valid_varname(char const *name, boole forenviron);

/* `shcodec' */
FL int c_shcodec(void *vp);

/*
 * spam.c
 */

#ifdef mx_HAVE_SPAM
/* Direct mappings of the various spam* commands */
FL int c_spam_clear(void *v);
FL int c_spam_set(void *v);
FL int c_spam_forget(void *v);
FL int c_spam_ham(void *v);
FL int c_spam_rate(void *v);
FL int c_spam_spam(void *v);
#endif

/*
 * strings.c
 */

/* Return a pointer to a dynamic copy of the argument */
FL char *savestr(char const *str  su_DBG_LOC_ARGS_DECL);
FL char *savestrbuf(char const *sbuf, uz slen  su_DBG_LOC_ARGS_DECL);
#ifdef su_HAVE_DBG_LOC_ARGS
# define savestr(CP) savestr(CP  su_DBG_LOC_ARGS_INJ)
# define savestrbuf(CBP,CBL) savestrbuf(CBP, CBL  su_DBG_LOC_ARGS_INJ)
#endif

/* Concatenate cp2 onto cp1 (if not NULL), separated by sep (if not '\0') */
FL char *savecatsep(char const *cp1, char sep, char const *cp2
   su_DBG_LOC_ARGS_DECL);
#ifdef su_HAVE_DBG_LOC_ARGS
# define savecatsep(S1,SEP,S2) savecatsep(S1, SEP, S2  su_DBG_LOC_ARGS_INJ)
#endif

/* Make copy of argument incorporating old one, if set, separated by space */
#define save2str(S,O)            savecatsep(O, ' ', S)

/* strcat */
#define savecat(S1,S2)           savecatsep(S1, '\0', S2)

/*  */
FL struct str * str_concat_csvl(struct str *self, ...);

/*  */
FL struct str *str_concat_cpa(struct str *self, char const * const *cpa,
   char const *sep_o_null  su_DBG_LOC_ARGS_DECL);
#ifdef su_HAVE_DBG_LOC_ARGS
# define str_concat_cpa(S,A,N) str_concat_cpa(S, A, N  su_DBG_LOC_ARGS_INJ)
#endif

/* Plain char* support, not auto-reclaimed (unless noted) */

/* Could the string contain a regular expression?
 * NOTE: on change: manual contains several occurrences of this string! */
#define n_is_maybe_regex(S) n_is_maybe_regex_buf(S, su_UZ_MAX)
FL boole n_is_maybe_regex_buf(char const *buf, uz len);

/* Convert a string to lowercase, in-place and with multibyte-aware */
FL void        makelow(char *cp);

/* Is *sub* a substring of *str*, case-insensitive and multibyte-aware? */
FL boole      substr(char const *str, char const *sub);

/* struct str related support funs TODO _cp->_cs! */

/* *self->s* is n_realloc()ed */
#define n_str_dup(S, T)          n_str_assign_buf((S), (T)->s, (T)->l)

/* *self->s* is n_realloc()ed; if buflen==UZ_MAX su_cs_len() is called unless
 * buf is NULL; buf may be NULL if buflen is 0 */
FL struct str *n_str_assign_buf(struct str *self, char const *buf, uz buflen
      su_DBG_LOC_ARGS_DECL);
#define n_str_assign(S, T)       n_str_assign_buf(S, (T)->s, (T)->l)
#define n_str_assign_cp(S, CP)   n_str_assign_buf(S, CP, UZ_MAX)

/* *self->s* is n_realloc()ed, *self->l* incremented; if buflen==UZ_MAX
 * su_cs_len() is called unless buf is NULL; buf may be NULL if buflen is 0 */
FL struct str *n_str_add_buf(struct str *self, char const *buf, uz buflen
      su_DBG_LOC_ARGS_DECL);
#define n_str_add(S, T)          n_str_add_buf(S, (T)->s, (T)->l)
#define n_str_add_cp(S, CP)      n_str_add_buf(S, CP, UZ_MAX)

#ifdef su_HAVE_DBG_LOC_ARGS
# define n_str_assign_buf(S,B,BL) \
   n_str_assign_buf(S, B, BL  su_DBG_LOC_ARGS_INJ)
# define n_str_add_buf(S,B,BL) n_str_add_buf(S, B, BL  su_DBG_LOC_ARGS_INJ)
#endif

/* Remove leading and trailing su_cs_is_space()s and *ifs-ws*, respectively.
 * The ->s and ->l of the string will be adjusted, but no NUL termination will
 * be applied to a possibly adjusted buffer!
 * If dofaults is set, " \t\n" is always trimmed (in addition).
 * Note trimming does not copy, it only adjusts the pointer/length */
FL struct str *n_str_trim(struct str *self, enum n_str_trim_flags stf);
FL struct str *n_str_trim_ifs(struct str *self, boole dodefaults);

/* struct n_string
 * May have NIL buffer, may contain embedded NULs */

FL struct n_string *n__string_clear(struct n_string *self);

/* Lifetime.  n_string_gut() is optional for _creat_auto() strings */
INLINE struct n_string *
n_string_creat(struct n_string *self){
   self->s_dat = NIL;
   self->s_len = self->s_auto = self->s_size = 0;
   return self;
}

INLINE struct n_string *
n_string_creat_auto(struct n_string *self){
   self->s_dat = NIL;
   self->s_len = self->s_auto = self->s_size = 0;
   self->s_auto = TRU1;
   return self;
}

INLINE void n_string_gut(struct n_string *self){
   if(self->s_dat != NIL)
      n__string_clear(self);
}

INLINE struct n_string *
n_string_trunc(struct n_string *self, uz len){
   ASSERT(UCMP(z, len, <=, self->s_len));
   self->s_len = S(u32,len);
   return self;
}

INLINE struct n_string *
n_string_take_ownership(struct n_string *self, char *buf, u32 size, u32 len){
   ASSERT(self->s_dat == NIL);
   ASSERT(size == 0 || buf != NIL);
   ASSERT(len == 0 || len < size);
   self->s_dat = buf;
   self->s_size = size;
   self->s_len = len;
   return self;
}

INLINE struct n_string *
n_string_drop_ownership(struct n_string *self){
   self->s_dat = NIL;
   self->s_len = self->s_size = 0;
   return self;
}

INLINE struct n_string *
n_string_clear(struct n_string *self){
   if(self->s_size > 0)
      self = n__string_clear(self);
   return self;
}

/* Check whether a buffer of Len bytes can be inserted into S(elf) */
INLINE boole n_string_get_can_book(uz len){
   return (S(uz,S32_MAX) - Z_ALIGN(1) > len);
}

INLINE boole n_string_can_book(struct n_string *self, uz len){
   return (n_string_get_can_book(len) &&
      S(uz,S32_MAX) - Z_ALIGN(1) - len > self->s_len);
}

/* Reserve room for noof additional bytes, but don't adjust length (yet) */
FL struct n_string *n_string_reserve(struct n_string *self, uz noof
      su_DBG_LOC_ARGS_DECL);
#define n_string_book n_string_reserve

/* Resize to exactly nlen bytes; any new storage isn't initialized */
FL struct n_string *n_string_resize(struct n_string *self, uz nlen
      su_DBG_LOC_ARGS_DECL);

#ifdef su_HAVE_DBG_LOC_ARGS
# define n_string_reserve(S,N)   (n_string_reserve)(S, N  su_DBG_LOC_ARGS_INJ)
# define n_string_resize(S,N)    (n_string_resize)(S, N  su_DBG_LOC_ARGS_INJ)
#endif

/* */
FL struct n_string *n_string_push_buf(struct n_string *self, char const *buf,
      uz buflen  su_DBG_LOC_ARGS_DECL);
#define n_string_push(S, T)       n_string_push_buf(S, (T)->s_len, (T)->s_dat)
#define n_string_push_cp(S,CP)    n_string_push_buf(S, CP, UZ_MAX)
FL struct n_string *n_string_push_c(struct n_string *self, char c
      su_DBG_LOC_ARGS_DECL);

#define n_string_assign(S,T)     ((S)->s_len = 0, n_string_push(S, T))
#define n_string_assign_c(S,C)   ((S)->s_len = 0, n_string_push_c(S, C))
#define n_string_assign_cp(S,CP) ((S)->s_len = 0, n_string_push_cp(S, CP))
#define n_string_assign_buf(S,B,BL) \
   ((S)->s_len = 0, n_string_push_buf(S, B, BL))

#ifdef su_HAVE_DBG_LOC_ARGS
# define n_string_push_buf(S,B,BL) \
   (n_string_push_buf)(S, B, BL  su_DBG_LOC_ARGS_INJ)
# define n_string_push_c(S,C) (n_string_push_c)(S, C  su_DBG_LOC_ARGS_INJ)
#endif

/* */
FL struct n_string *n_string_unshift_buf(struct n_string *self,
      char const *buf, uz buflen  su_DBG_LOC_ARGS_DECL);
#define n_string_unshift(S,T) \
   n_string_unshift_buf(S, (T)->s_len, (T)->s_dat)
#define n_string_unshift_cp(S,CP) \
   n_string_unshift_buf(S, CP, UZ_MAX)
FL struct n_string *n_string_unshift_c(struct n_string *self, char c
      su_DBG_LOC_ARGS_DECL);

#ifdef su_HAVE_DBG_LOC_ARGS
# define n_string_unshift_buf(S,B,BL) \
   (n_string_unshift_buf)(S, B, BL  su_DBG_LOC_ARGS_INJ)
# define n_string_unshift_c(S,C) \
   (n_string_unshift_c)(S, C  su_DBG_LOC_ARGS_INJ)
#endif

/* */
FL struct n_string *n_string_insert_buf(struct n_string *self, uz idx,
      char const *buf, uz buflen  su_DBG_LOC_ARGS_DECL);
#define n_string_insert(S,I,T) \
   n_string_insert_buf(S, I, (T)->s_len, (T)->s_dat)
#define n_string_insert_cp(S,I,CP) \
   n_string_insert_buf(S, I, CP, UZ_MAX)
FL struct n_string *n_string_insert_c(struct n_string *self, uz idx,
      char c  su_DBG_LOC_ARGS_DECL);

#ifdef su_HAVE_DBG_LOC_ARGS
# define n_string_insert_buf(S,I,B,BL) \
   (n_string_insert_buf)(S, I, B, BL  su_DBG_LOC_ARGS_INJ)
# define n_string_insert_c(S,I,C) \
   (n_string_insert_c)(S, I, C  su_DBG_LOC_ARGS_INJ)
#endif

/* */
FL struct n_string *n_string_cut(struct n_string *self, uz idx,
      uz len);

/* Ensure self has a - NUL terminated - buffer, and return that.
 * The latter may return the pointer to an internal empty RODATA instead */
FL char *n_string_cp(struct n_string *self  su_DBG_LOC_ARGS_DECL);
FL char const *n_string_cp_const(struct n_string const *self);

#ifdef su_HAVE_DBG_LOC_ARGS
# define n_string_cp(S) (n_string_cp)(S  su_DBG_LOC_ARGS_INJ)
#endif

/*
 * thread.c
 */

/*  */
FL int         c_thread(void *vp);

/*  */
FL int         c_unthread(void *vp);

/*  */
FL struct message * next_in_thread(struct message *mp);
FL struct message * prev_in_thread(struct message *mp);
FL struct message * this_in_thread(struct message *mp, long n);

/* Sorted mode is internally just a variant of threaded mode with all m_parent
 * and m_child links being NULL */
FL int         c_sort(void *vp);

/*  */
FL int         c_collapse(void *v);
FL int         c_uncollapse(void *v);

/*  */
FL void        uncollapse1(struct message *mp, int always);

/*
 * tls.c
 */

#ifdef mx_HAVE_TLS
/*  */
FL void n_tls_set_verify_level(struct mx_url const *urlp);

/* */
FL boole n_tls_verify_decide(void);

/*  */
FL boole mx_smime_split(FILE *ip, FILE **hp, FILE **bp, long xcount,
      boole keep);

/* */
FL FILE *      smime_sign_assemble(FILE *hp, FILE *bp, FILE *sp,
                  char const *message_digest);

/*  */
FL FILE *      smime_encrypt_assemble(FILE *hp, FILE *yp);

/* hp and bp are NOT closed */
FL struct message *mx_smime_decrypt_assemble(struct message *mp, FILE *hp,
      FILE *bp);

/* `certsave' */
FL int c_certsave(void *vp);

/* */
FL boole n_tls_rfc2595_hostname_match(char const *host, char const *pattern);

/* `tls' */
FL int c_tls(void *vp);
#endif /* mx_HAVE_TLS */

/*
 * xtls.c
 */

#ifdef mx_HAVE_XTLS
/* Our wrapper for RAND_bytes(3); the implementation exists only when
 * HAVE_RANDOM is RANDOM_IMPL_TLS, though */
FL void mx_tls_rand_bytes(void *buf, uz blen);

/* Will fill in a non-NULL *urlp->url_cert_fprint with auto-reclaimed
 * buffer on success, otherwise urlp is constant */
FL boole n_tls_open(struct mx_url *urlp, struct mx_socket *sp);

/*  */
FL void        ssl_gen_err(char const *fmt, ...);

/*  */
FL int         c_verify(void *vp);

/*  */
FL FILE *      smime_sign(FILE *ip, char const *addr);

/*  */
FL FILE *      smime_encrypt(FILE *ip, char const *certfile, char const *to);

FL struct message * smime_decrypt(struct message *m, char const *to,
                     char const *cc, boole is_a_verify_call);

/*  */
FL enum okay   smime_certsave(struct message *m, int n, FILE *op);

#endif /* mx_HAVE_XTLS */

/*
 * obs-imap.c
 */

#ifdef mx_HAVE_IMAP
FL void n_go_onintr_for_imap(void);

/* The former returns the input again if no conversion is necessary */
FL char const *imap_path_encode(char const *path, boole *err_or_null);
FL char *imap_path_decode(char const *path, boole *err_or_null);

FL char const * imap_fileof(char const *xcp);
FL enum okay   imap_noop(void);
FL enum okay   imap_select(struct mailbox *mp, off_t *size, int *count,
                  const char *mbx, enum fedit_mode fm);
FL int imap_setfile(char const *who, const char *xserver, enum fedit_mode fm);
FL enum okay   imap_header(struct message *m);
FL enum okay   imap_body(struct message *m);
FL void        imap_getheaders(int bot, int top);
FL boole      imap_quit(boole hold_sigs_on);
FL enum okay   imap_undelete(struct message *m, int n);
FL enum okay   imap_unread(struct message *m, int n);
FL int         c_imapcodec(void *vp);
FL int         c_imap_imap(void *vp);
FL int         imap_newmail(int nmail);
FL enum okay   imap_append(const char *xserver, FILE *fp, long offset);
FL int         imap_folders(const char *name, int strip);
FL enum okay   imap_copy(struct message *m, int n, const char *name);
# ifdef mx_HAVE_IMAP_SEARCH
FL sz     imap_search1(const char *spec, int f);
# endif
FL int         imap_thisaccount(const char *cp);
FL enum okay   imap_remove(const char *name);
FL enum okay   imap_rename(const char *old, const char *new);
FL enum okay   imap_dequeue(struct mailbox *mp, FILE *fp);
FL int         c_connect(void *vp);
FL int         c_disconnect(void *vp);
FL int         c_cache(void *vp);
FL int         disconnected(const char *file);
FL void        transflags(struct message *omessage, long omsgCount,
                  int transparent);
FL time_t      imap_read_date_time(const char *cp);
FL const char * imap_make_date_time(time_t t);

/* Extract the protocol base and return a duplicate */
FL char *protbase(char const *cp  su_DBG_LOC_ARGS_DECL);
# ifdef su_HAVE_DBG_LOC_ARGS
#  define protbase(CP) (protbase)(CP  su_DBG_LOC_ARGS_INJ)
# endif
#endif /* mx_HAVE_IMAP */

/*
 * obs-imap-cache.c
 */

#ifdef mx_HAVE_IMAP
FL enum okay   getcache1(struct mailbox *mp, struct message *m,
                  enum needspec need, int setflags);
FL enum okay   getcache(struct mailbox *mp, struct message *m,
                  enum needspec need);
FL void        putcache(struct mailbox *mp, struct message *m);
FL void        initcache(struct mailbox *mp);
FL void        purgecache(struct mailbox *mp, struct message *m, long mc);
FL void        delcache(struct mailbox *mp, struct message *m);
FL enum okay   cache_setptr(enum fedit_mode fm, int transparent);
FL enum okay   cache_list(struct mailbox *mp, char const *base, int strip,
                  FILE *fp);
FL enum okay   cache_remove(char const *name);
FL enum okay   cache_rename(char const *old, char const *new);
FL u64 cached_uidvalidity(struct mailbox *mp);
FL FILE *      cache_queue(struct mailbox *mp);
FL enum okay   cache_dequeue(struct mailbox *mp);
#endif /* mx_HAVE_IMAP */

/*
 * obs-lzw.c
 */
#ifdef mx_HAVE_IMAP
FL int         zwrite(void *cookie, const char *wbp, int num);
FL int         zfree(void *cookie);
FL int         zread(void *cookie, char *rbp, int num);
FL void *      zalloc(FILE *fp);
#endif /* mx_HAVE_IMAP */

#ifndef mx_HAVE_AMALGAMATION
# undef FL
# define FL
#endif

/* s-it-mode */
