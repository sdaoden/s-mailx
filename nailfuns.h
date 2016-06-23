/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Function prototypes and function-alike macros.
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

/*
 * TODO Convert optional utility+ functions to n_*(); ditto
 * TODO else use generic module-specific prefixes: str_(), am[em]_, sm[em]_, ..
 */
/* TODO s-it-mode: not really (docu, funnames, funargs, etc) */

#undef FL
#ifndef HAVE_AMALGAMATION
# define FL                      extern
#else
# define FL                      static
#endif

/* Memory allocation routines from memory.c offer some debug support */
#if (defined HAVE_DEBUG || defined HAVE_DEVEL) && !defined HAVE_NOMEMDBG
# define HAVE_MEMORY_DEBUG
# define SMALLOC_DEBUG_ARGS      , char const *mdbg_file, int mdbg_line
# define SMALLOC_DEBUG_ARGSCALL  , mdbg_file, mdbg_line
# define SALLOC_DEBUG_ARGS       , char const *mdbg_file, int mdbg_line
# define SALLOC_DEBUG_ARGSCALL   , mdbg_file, mdbg_line
#else
# define SMALLOC_DEBUG_ARGS
# define SMALLOC_DEBUG_ARGSCALL
# define SALLOC_DEBUG_ARGS
# define SALLOC_DEBUG_ARGSCALL
#endif

/*
 * Macro-based generics
 */

/* ASCII char classification */
#define __ischarof(C, FLAGS)  \
   (asciichar(C) && (class_char[(ui8_t)(C)] & (FLAGS)) != 0)

#define n_uasciichar(U) ((size_t)(U) <= 0x7F)
#define asciichar(c)    ((uc_i)(c) <= 0x7F)
#define alnumchar(c)    __ischarof(c, C_DIGIT | C_OCTAL | C_UPPER | C_LOWER)
#define alphachar(c)    __ischarof(c, C_UPPER | C_LOWER)
#define blankchar(c)    __ischarof(c, C_BLANK)
#define blankspacechar(c) __ischarof(c, C_BLANK | C_SPACE)
#define cntrlchar(c)    __ischarof(c, C_CNTRL)
#define digitchar(c)    __ischarof(c, C_DIGIT | C_OCTAL)
#define lowerchar(c)    __ischarof(c, C_LOWER)
#define punctchar(c)    __ischarof(c, C_PUNCT)
#define spacechar(c)    __ischarof(c, C_BLANK | C_SPACE | C_WHITE)
#define upperchar(c)    __ischarof(c, C_UPPER)
#define whitechar(c)    __ischarof(c, C_BLANK | C_WHITE)
#define octalchar(c)    __ischarof(c, C_OCTAL)
#define hexchar(c) /* TODO extend bits, add C_HEX */\
   (__ischarof(c, C_DIGIT | C_OCTAL) || ((c) >= 'A' && (c) <= 'F') ||\
    ((c) >= 'a' && (c) <= 'f'))

#define upperconv(c)    (lowerchar(c) ? (char)((uc_i)(c) - 'a' + 'A') : (c))
#define lowerconv(c)    (upperchar(c) ? (char)((uc_i)(c) - 'A' + 'a') : (c))
/* RFC 822, 3.2. */
#define fieldnamechar(c) \
   (asciichar(c) && (c) > 040 && (c) != 0177 && (c) != ':')

/* Could the string contain a regular expression? */
#if 0
# define is_maybe_regex(S) anyof("^.[]*+?()|$", S)
#else
# define is_maybe_regex(S) anyof("^[]*+?|$", S)
#endif

/* Single-threaded, use unlocked I/O */
#ifdef HAVE_PUTC_UNLOCKED
# undef getc
# define getc(c)        getc_unlocked(c)
# undef putc
# define putc(c, f)     putc_unlocked(c, f)
# undef putchar
# define putchar(c)     putc_unlocked((c), stdout)
#endif

/* Truncate a file to the last character written.  This is useful just before
 * closing an old file that was opened for read/write */
#define ftrunc(stream) \
do {\
   off_t off;\
   fflush(stream);\
   off = ftell(stream);\
   if (off >= 0)\
      ftruncate(fileno(stream), off);\
} while (0)

/* fflush() and rewind() */
#define fflush_rewind(stream) \
do {\
   fflush(stream);\
   rewind(stream);\
} while (0)

/* There are problems with dup()ing of file-descriptors for child processes.
 * As long as those are not fixed in equal spirit to (outof(): FIX and
 * recode.., 2012-10-04), and to avoid reviving of bugs like (If *record* is
 * set, avoid writing dead content twice.., 2012-09-14), we have to somehow
 * accomplish that the FILE* fp makes itself comfortable with the *real* offset
 * of the underlaying file descriptor.  Unfortunately Standard I/O and POSIX
 * don't describe a way for that -- fflush();rewind(); won't do it.  This
 * fseek(END),rewind() pair works around the problem on *BSD and Linux.
 * Update as of 2014-03-03: with Issue 7 POSIX has overloaded fflush(3): if
 * used on a readable stream, then
 *
 *    if the file is not already at EOF, and the file is one capable of
 *    seeking, the file offset of the underlying open file description shall
 *    be set to the file position of the stream.
 *
 * We need our own, simplified and reliable I/O */
#if defined _POSIX_VERSION && _POSIX_VERSION + 0 >= 200809L
# define really_rewind(stream) \
do {\
   rewind(stream);\
   fflush(stream);\
} while (0)
#else
# define really_rewind(stream) \
do {\
   rewind(stream);\
   fflush(stream);\
   lseek(fileno(stream), 0, SEEK_SET);\
} while (0)
#endif

/*
 * accmacvar.c
 */

/* Macros: `define', `undefine', `call' / `~' */
FL int         c_define(void *v);
FL int         c_undefine(void *v);
FL int         c_call(void *v);

/* TODO Check wether a *folder-hook* exists for the currently active mailbox */
FL bool_t      check_folder_hook(bool_t nmail);

/* TODO v15 drop Invoke compose hook macname */
FL void        call_compose_mode_hook(char const *macname,
                  void (*hook_pre)(void *), void *hook_arg);

/* Accounts: `account', `unaccount' */
FL int         c_account(void *v);
FL int         c_unaccount(void *v);

/* `localopts' */
FL int         c_localopts(void *v);

FL void        temporary_localopts_free(void); /* XXX intermediate hack */
FL void        temporary_localopts_folder_hook_unroll(void); /* XXX im. hack */

/* Don't use n_var_* unless you *really* have to! */

/* Constant option key look/(un)set/clear */
FL char *      n_var_oklook(enum okeys okey);
#define ok_blook(C)              (n_var_oklook(CONCAT(ok_b_, C)) != NULL)
#define ok_vlook(C)              n_var_oklook(CONCAT(ok_v_, C))

FL bool_t      n_var_okset(enum okeys okey, uintptr_t val);
#define ok_bset(C,B)             n_var_okset(CONCAT(ok_b_, C), (uintptr_t)(B))
#define ok_vset(C,V)             n_var_okset(CONCAT(ok_v_, C), (uintptr_t)(V))

FL bool_t      n_var_okclear(enum okeys okey);
#define ok_bclear(C)             n_var_okclear(CONCAT(ok_b_, C))
#define ok_vclear(C)             n_var_okclear(CONCAT(ok_v_, C))

/* Variable option key look/(un)set/clear */
FL char *      n_var_voklook(char const *vokey);
#define vok_blook(S)              (n_var_voklook(S) != NULL)
#define vok_vlook(S)              n_var_voklook(S)

FL bool_t      n_var_vokset(char const *vokey, uintptr_t val);
#define vok_bset(S,B)            n_var_vokset(S, (uintptr_t)(B))
#define vok_vset(S,V)            n_var_vokset(S, (uintptr_t)(V))

FL bool_t      n_var_vokclear(char const *vokey);
#define vok_bclear(S)            n_var_vokclear(S)
#define vok_vclear(S)            n_var_vokclear(S)

/* Special case to handle the typical [xy-USER@HOST,] xy-HOST and plain xy
 * variable chains; oxm is a bitmix which tells which combinations to test */
#ifdef HAVE_SOCKETS
FL char *      n_var_xoklook(enum okeys okey, struct url const *urlp,
                  enum okey_xlook_mode oxm);
# define xok_BLOOK(C,URL,M)      (n_var_xoklook(C, URL, M) != NULL)
# define xok_VLOOK(C,URL,M)      n_var_xoklook(C, URL, M)
# define xok_blook(C,URL,M)      xok_BLOOK(CONCAT(ok_b_, C), URL, M)
# define xok_vlook(C,URL,M)      xok_VLOOK(CONCAT(ok_v_, C), URL, M)
#endif

/* User variable access: `set' and `unset' */
FL int         c_set(void *v);
FL int         c_unset(void *v);

/* `varshow' */
FL int         c_varshow(void *v);

/* Ditto: `varedit' */
FL int         c_varedit(void *v);

/* `environ' */
FL int         c_environ(void *v);

/*
 * attachments.c
 */

/* Try to add an attachment for file, file_expand()ed.
 * Return the new head of list aphead, or NULL.
 * The newly created attachment will be stored in *newap, if given */
FL struct attachment * add_attachment(struct attachment *aphead, char *file,
                        struct attachment **newap);

/* Append comma-separated list of file names to the end of attachment list */
FL void        append_attachments(struct attachment **aphead, char *names);

/* Interactively edit the attachment list */
FL void        edit_attachments(struct attachment **aphead);

/*
 * auxlily.c
 */

/* Compute screen size */
FL int         screensize(void);

/* Get our $PAGER; if env_addon is not NULL it is checked wether we know about
 * some environment variable that supports colour+ and set *env_addon to that,
 * e.g., "LESS=FRSXi" */
FL char const *n_pager_get(char const **env_addon);

/* Use a pager or STDOUT to print *fp*; if *lines* is 0, they'll be counted */
FL void        page_or_print(FILE *fp, size_t lines);

/* Parse name and guess at the required protocol */
FL enum protocol  which_protocol(char const *name);

/* Hexadecimal itoa (NUL terminates) / atoi (-1 on error) */
FL char *      n_c_to_hex_base16(char store[3], char c);
FL si32_t      n_c_from_hex_base16(char const hex[2]);

/* Hash the passed string -- uses Chris Torek's hash algorithm */
FL ui32_t      torek_hash(char const *name);
#define hash(S)                  (torek_hash(S) % HSHSIZE) /* xxx COMPAT (?) */

/* Create hash */
FL ui32_t      pjw(char const *cp); /* TODO obsolete -> torek_hash() */

/* Find a prime greater than n */
FL ui32_t      nextprime(ui32_t n);

/* Get *prompt*, or '& ' if *bsdcompat*, of '? ' otherwise */
FL char *      getprompt(void);

/* Detect and query the hostname to use */
FL char *      nodename(int mayoverride);

/* Get a (pseudo) random string of *length* bytes; returns salloc()ed buffer */
FL char *      getrandstring(size_t length);

/* Check wether the argument string is a true (1) or false (0) boolean, or an
 * invalid string, in which case -1 is returned; if emptyrv is not -1 then it,
 * treated as a boolean, is used as the return value shall inbuf be empty.
 * inlen may be UIZ_MAX to force strlen() detection */
FL si8_t       boolify(char const *inbuf, uiz_t inlen, si8_t emptyrv);

/* Dig a "quadoption" in inbuf (possibly going through getapproval() in
 * interactive mode).  Returns a boolean or -1 if inbuf content is invalid;
 * if emptyrv is not -1 then it,  treated as a boolean, is used as the return
 * value shall inbuf be empty.  If prompt is set it is printed first if intera.
 * inlen may be UIZ_MAX to force strlen() detection */
FL si8_t       quadify(char const *inbuf, uiz_t inlen, char const *prompt,
                  si8_t emptyrv);

/* Is the argument "all" (case-insensitive) or "*" */
FL bool_t      n_is_all_or_aster(char const *name);

/* Get seconds since epoch */
FL time_t      n_time_epoch(void);

/* Update *tc* to now; only .tc_time updated unless *full_update* is true */
FL void        time_current_update(struct time_current *tc,
                  bool_t full_update);

/* Returns 0 if fully slept, number of millis left if ignint is true and we
 * were interrupted.  Actual resolution may be second or less.
 * Note in case of HAVE_SLEEP this may be SIGALARM based. */
FL uiz_t       n_msleep(uiz_t millis, bool_t ignint);

/* Our error print series..  Note: these reverse scan format in order to know
 * wether a newline was included or not -- this affects the output! */
FL void        n_err(char const *format, ...);
FL void        n_verr(char const *format, va_list ap);

/* ..(for use in a signal handler; to be obsoleted..).. */
FL void        n_err_sighdl(char const *format, ...);

/* Our perror(3); if errval is 0 errno(3) is used; newline appended */
FL void        n_perr(char const *msg, int errval);

/* Announce a fatal error (and die); newline appended */
FL void        n_alert(char const *format, ...);
FL void        n_panic(char const *format, ...);

/* `errors' */
#ifdef HAVE_ERRORS
FL int         c_errors(void *vp);
#else
# define c_errors                c_cmdnotsupp
#endif

/*
 * cmd1.c
 */

FL int         c_cmdnotsupp(void *v);

/* `headers' (show header group, possibly after setting dot) */
FL int         c_headers(void *v);

/* Like c_headers(), but pre-prepared message vector */
FL int         print_header_group(int *vector);

/* Scroll to the next/previous screen */
FL int         c_scroll(void *v);
FL int         c_Scroll(void *v);

/* Print out the headlines for each message in the passed message list */
FL int         c_from(void *v);

/* Print all message in between and including bottom and topx if they are
 * visible and either only_marked is false or they are MMARKed */
FL void        print_headers(size_t bottom, size_t topx, bool_t only_marked);

/* Print out the value of dot */
FL int         c_pdot(void *v);

/* Paginate messages, honor/don't honour ignored fields, respectively */
FL int         c_more(void *v);
FL int         c_More(void *v);

/* Type out messages, honor/don't honour ignored fields, respectively */
FL int         c_type(void *v);
FL int         c_Type(void *v);

/* Show MIME-encoded message text, including all fields */
FL int         c_show(void *v);

/* Pipe messages, honor/don't honour ignored fields, respectively */
FL int         c_pipe(void *v);
FL int         c_Pipe(void *v);

/* Print the top so many lines of each desired message.
 * The number of lines is taken from *toplines* and defaults to 5 */
FL int         c_top(void *v);

/* Touch all the given messages so that they will get mboxed */
FL int         c_stouch(void *v);

/* Make sure all passed messages get mboxed */
FL int         c_mboxit(void *v);

/* List the folders the user currently has */
FL int         c_folders(void *v);

/*
 * cmd2.c
 */

/* If any arguments were given, go to the next applicable argument following
 * dot, otherwise, go to the next applicable message.  If given as first
 * command with no arguments, print first message */
FL int         c_next(void *v);

/* Move the dot up or down by one message */
FL int         c_dotmove(void *v);

/* Save a message in a file.  Mark the message as saved so we can discard when
 * the user quits */
FL int         c_save(void *v);
FL int         c_Save(void *v);

/* Copy a message to a file without affected its saved-ness */
FL int         c_copy(void *v);
FL int         c_Copy(void *v);

/* Move a message to a file */
FL int         c_move(void *v);
FL int         c_Move(void *v);

/* Decrypt and copy a message to a file */
FL int         c_decrypt(void *v);
FL int         c_Decrypt(void *v);

/* Write the indicated messages at the end of the passed file name, minus
 * header and trailing blank line.  This is the MIME save function */
FL int         c_write(void *v);

/* Delete messages */
FL int         c_delete(void *v);

/* Delete messages, then type the new dot */
FL int         c_deltype(void *v);

/* Undelete the indicated messages */
FL int         c_undelete(void *v);

/* Add the given header fields to the retained list.  If no arguments, print
 * the current list of retained fields */
FL int         c_retfield(void *v);

/* Add the given header fields to the ignored list.  If no arguments, print the
 * current list of ignored fields */
FL int         c_igfield(void *v);

FL int         c_saveretfield(void *v);
FL int         c_saveigfield(void *v);
FL int         c_fwdretfield(void *v);
FL int         c_fwdigfield(void *v);
FL int         c_unignore(void *v);
FL int         c_unretain(void *v);
FL int         c_unsaveignore(void *v);
FL int         c_unsaveretain(void *v);
FL int         c_unfwdignore(void *v);
FL int         c_unfwdretain(void *v);

/*
 * cmd3.c
 */

/* Process a shell escape by saving signals, ignoring signals and a sh -c */
FL int         c_shell(void *v);

/* Fork an interactive shell */
FL int         c_dosh(void *v);

/* Show the help screen */
FL int         c_help(void *v);

/* Print user's working directory */
FL int         c_cwd(void *v);

/* Change user's working directory */
FL int         c_chdir(void *v);

/* All thinkable sorts of `reply' / `respond' and `followup'.. */
FL int         c_reply(void *v);
FL int         c_replyall(void *v);
FL int         c_replysender(void *v);
FL int         c_Reply(void *v);
FL int         c_followup(void *v);
FL int         c_followupall(void *v);
FL int         c_followupsender(void *v);
FL int         c_Followup(void *v);

/* ..and a mailing-list reply */
FL int         c_Lreply(void *v);

/* The 'forward' command */
FL int         c_forward(void *v);

/* Similar to forward, saving the message in a file named after the first
 * recipient */
FL int         c_Forward(void *v);

/* Resend a message list to a third person */
FL int         c_resend(void *v);

/* Resend a message list to a third person without adding headers */
FL int         c_Resend(void *v);

/* Preserve messages, so that they will be sent back to the system mailbox */
FL int         c_preserve(void *v);

/* Mark all given messages as unread */
FL int         c_unread(void *v);

/* Mark all given messages as read */
FL int         c_seen(void *v);

/* Print the size of each message */
FL int         c_messize(void *v);

/* `file' (`folder') and `File' (`Folder') */
FL int         c_file(void *v);
FL int         c_File(void *v);

/* Expand file names like echo */
FL int         c_echo(void *v);

/* 'newmail' command: Check for new mail without writing old mail back */
FL int         c_newmail(void *v);

/* Message flag manipulation */
FL int         c_flag(void *v);
FL int         c_unflag(void *v);
FL int         c_answered(void *v);
FL int         c_unanswered(void *v);
FL int         c_draft(void *v);
FL int         c_undraft(void *v);

/* noop */
FL int         c_noop(void *v);

/* Remove mailbox */
FL int         c_remove(void *v);

/* Rename mailbox */
FL int         c_rename(void *v);

/* `urlencode' and `urldecode' */
FL int         c_urlencode(void *v);
FL int         c_urldecode(void *v);

/*
 * cmd_cnd.c
 */

/* if.elif.else.endif conditional execution.
 * condstack_isskip() returns wether the current condition state doesn't allow
 * execution of commands.
 * condstack_release() and condstack_take() rotate the current condition stack;
 * condstack_take() returns a false boolean if the current condition stack has
 * unclosed conditionals */
FL int         c_if(void *v);
FL int         c_elif(void *v);
FL int         c_else(void *v);
FL int         c_endif(void *v);
FL bool_t      condstack_isskip(void);
FL void *      condstack_release(void);
FL bool_t      condstack_take(void *self);

/*
 * collect.c
 */

/*
 * If quotefile is (char*)-1, stdin will be used, caller has to verify that
 * we're not running in interactive mode */
FL FILE *      collect(struct header *hp, int printheaders, struct message *mp,
                  char *quotefile, int doprefix, si8_t *checkaddr_err);

FL void        savedeadletter(FILE *fp, int fflush_rewind_first);

/*
 * edit.c
 */

/* Edit a message list */
FL int         c_editor(void *v);

/* Invoke the visual editor on a message list */
FL int         c_visual(void *v);

/* Run an editor on either size bytes of the file fp (or until EOF if size is
 * negative) or on the message mp, and return a new file or NULL on error of if
 * the user didn't perform any edits.
 * For now we assert that mp==NULL if hp!=NULL, treating this as a special call
 * from within compose mode, and giving TRUM1 to puthead().
 * Signals must be handled by the caller.  viored is 'e' for ed, 'v' for vi */
FL FILE *      run_editor(FILE *fp, off_t size, int viored, int readonly,
                  struct header *hp, struct message *mp,
                  enum sendaction action, sighandler_type oldint);

/*
 * colour.c
 */

#ifdef HAVE_COLOUR
/* `(un)?colour' */
FL int         c_colour(void *v);
FL int         c_uncolour(void *v);

/* We want coloured output (in this salloc() cycle).  pager_used is used to
 * test wether *colour-pager* is to be inspected.
 * The push/pop functions deal with recursive execute()ions, for now. TODO
 * env_gut() will reset() as necessary */
FL void        n_colour_env_create(enum n_colour_ctx cctx, bool_t pager_used);
FL void        n_colour_env_push(void);
FL void        n_colour_env_pop(bool_t any_env_till_root);
FL void        n_colour_env_gut(FILE *fp);

/* Putting anything (for pens: including NULL) resets current state first */
FL void        n_colour_put(FILE *fp, enum n_colour_id cid, char const *ctag);
FL void        n_colour_reset(FILE *fp);

/* Of course temporary only and may return NULL.  Doesn't affect state! */
FL struct str const *n_colour_reset_to_str(void);

/* A pen is bound to its environment and automatically reclaimed; it may be
 * NULL but that can be used anyway for simplicity.
 * This includes pen_to_str() -- which doesn't affect state! */
FL struct n_colour_pen *n_colour_pen_create(enum n_colour_id cid,
                           char const *ctag);
FL void        n_colour_pen_put(struct n_colour_pen *self, FILE *fp);

FL struct str const *n_colour_pen_to_str(struct n_colour_pen *self);

#else /* HAVE_COLOUR */
# define c_colour                c_cmdnotsupp
# define c_uncolour              c_cmdnotsupp
# define c_mono                  c_cmdnotsupp
# define c_unmono                c_cmdnotsupp
#endif

/*
 * dotlock.c
 */

/* Aquire a flt lock and create a dotlock file; upon success a registered
 * control-pipe FILE* is returned that keeps the link in between us and the
 * lock-holding fork(2)ed subprocess (which conditionally replaced itself via
 * execv(2) with the privilege-separated dotlock helper program): the lock file
 * will be removed once the control pipe is closed via Pclose().
 * Will try FILE_LOCK_TRIES times if pollmsecs > 0 (once otherwise).
 * If *dotlock_ignore_error* is set (FILE*)-1 will be returned if at least the
 * normal file lock could be established, otherwise errno is usable on error */
FL FILE *      n_dotlock(char const *fname, int fd, enum n_file_lock_type flt,
                  off_t off, off_t len, size_t pollmsecs);

/*
 * filter.c
 */

/* Quote filter */
FL struct quoteflt * quoteflt_dummy(void); /* TODO LEGACY */
FL void        quoteflt_init(struct quoteflt *self, char const *prefix);
FL void        quoteflt_destroy(struct quoteflt *self);
FL void        quoteflt_reset(struct quoteflt *self, FILE *f);
FL ssize_t     quoteflt_push(struct quoteflt *self, char const *dat,
                  size_t len);
FL ssize_t     quoteflt_flush(struct quoteflt *self);

/* (Primitive) HTML tagsoup filter */
#ifdef HAVE_FILTER_HTML_TAGSOUP
/* TODO Because we don't support filter chains yet this filter will be run
 * TODO in a dedicated subprocess, driven via a special Popen() mode */
FL int         htmlflt_process_main(void);

FL void        htmlflt_init(struct htmlflt *self);
FL void        htmlflt_destroy(struct htmlflt *self);
FL void        htmlflt_reset(struct htmlflt *self, FILE *f);
FL ssize_t     htmlflt_push(struct htmlflt *self, char const *dat, size_t len);
FL ssize_t     htmlflt_flush(struct htmlflt *self);
#endif

/*
 * fio.c
 */

/* fgets() replacement to handle lines of arbitrary size and with embedded \0
 * characters.
 * line - line buffer.  *line may be NULL.
 * linesize - allocated size of line buffer.
 * count - maximum characters to read.  May be NULL.
 * llen - length_of_line(*line).
 * fp - input FILE.
 * appendnl - always terminate line with \n, append if necessary.
 */
FL char *      fgetline(char **line, size_t *linesize, size_t *count,
                  size_t *llen, FILE *fp, int appendnl SMALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define fgetline(A,B,C,D,E,F)   \
   fgetline(A, B, C, D, E, F, __FILE__, __LINE__)
#endif

/* Read up a line from the specified input into the linebuffer.
 * Return the number of characters read.  Do not include the newline at EOL.
 * n is the number of characters already read */
FL int         readline_restart(FILE *ibuf, char **linebuf, size_t *linesize,
                  size_t n SMALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define readline_restart(A,B,C,D) \
   readline_restart(A, B, C, D, __FILE__, __LINE__)
#endif

/* Set up the input pointers while copying the mail file into /tmp */
FL void        setptr(FILE *ibuf, off_t offset);

/* Drop the passed line onto the passed output buffer.  If a write error occurs
 * return -1, else the count of characters written, including the newline */
FL int         putline(FILE *obuf, char *linebuf, size_t count);

/* Determine the size of the file possessed by the passed buffer */
FL off_t       fsize(FILE *iob);

/* Return the name of the dead.letter file */
FL char const * getdeadletter(void);

/* Will retry FILE_LOCK_RETRIES times if pollmsecs > 0 */
FL bool_t      n_file_lock(int fd, enum n_file_lock_type flt,
                  off_t off, off_t len, size_t pollmsecs);

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

/* Announce the presence of the current Mail version, give the message count,
 * and print a header listing */
FL void        announce(int printheaders);

/* Announce information about the file we are editing.  Return a likely place
 * to set dot */
FL int         newfileinfo(void);

FL int         getmdot(int nmail);

FL void        initbox(char const *name);

/* Determine and expand the current *folder* name, return it or the empty
 * string also in case of errors: since POSIX mandates a default of CWD if not
 * set etc., that seems to be a valid fallback, then */
FL char const *folder_query(void);

/*
 * head.c
 */

/* Return the user's From: address(es) */
FL char const * myaddrs(struct header *hp);

/* Boil the user's From: addresses down to a single one, or use *sender* */
FL char const * myorigin(struct header *hp);

/* See if the passed line buffer, which may include trailing newline (sequence)
 * is a mail From_ header line according to RFC 4155.
 * If compat is true laxe POSIX syntax is instead sufficient to match From_ */
FL int         is_head(char const *linebuf, size_t linelen, bool_t compat);

/* Savage extract date field from From_ line.  linelen is convenience as line
 * must be terminated (but it may end in a newline [sequence]).
 * Return wether the From_ line was parsed successfully */
FL int         extract_date_from_from_(char const *line, size_t linelen,
                  char datebuf[FROM_DATEBUF]);

/* Extract some header fields (see e.g. -t documentation) from a message.
 * If options&OPT_t_FLAG *and* pstate&PS_t_FLAG are both set a number of
 * additional header fields are understood and address joining is performed as
 * necessary, and the subject is treated with additional care, too.
 * If pstate&PS_t_FLAG is set but OPT_t_FLAG is no more, From: will not be
 * assigned no more.
 * This calls expandaddr() on some headers and sets checkaddr_err if that is
 * not NULL -- note it explicitly allows EAF_NAME because aliases are not
 * expanded when this is called! */
FL void        extract_header(FILE *fp, struct header *hp,
                  si8_t *checkaddr_err);

/* Return the desired header line from the passed message
 * pointer (or NULL if the desired header field is not available).
 * If mult is zero, return the content of the first matching header
 * field only, the content of all matching header fields else */
FL char *      hfield_mult(char const *field, struct message *mp, int mult);
#define hfieldX(a, b)            hfield_mult(a, b, 1)
#define hfield1(a, b)            hfield_mult(a, b, 0)

/* Check whether the passed line is a header line of the desired breed.
 * Return the field body, or 0 */
FL char const * thisfield(char const *linebuf, char const *field);

/* Get sender's name from this message.  If the message has a bunch of arpanet
 * stuff in it, we may have to skin the name before returning it */
FL char *      nameof(struct message *mp, int reptype);

/* Start of a "comment".  Ignore it */
FL char const * skip_comment(char const *cp);

/* Return the start of a route-addr (address in angle brackets), if present */
FL char const * routeaddr(char const *name);

/* Query *expandaddr*, parse it and return flags.
 * The flags are already adjusted for OPT_INTERACTIVE / OPT_TILDE_FLAG etc. */
FL enum expand_addr_flags expandaddr_to_eaf(void);

/* Check if an address is invalid, either because it is malformed or, if not,
 * according to eacm.  Return FAL0 when it looks good, TRU1 if it is invalid
 * but the error condition wasn't covered by a 'hard "fail"ure', -1 otherwise */
FL si8_t       is_addr_invalid(struct name *np,
                  enum expand_addr_check_mode eacm);

/* Does *NP* point to a file or pipe addressee? */
#define is_fileorpipe_addr(NP)   \
   (((NP)->n_flags & NAME_ADDRSPEC_ISFILEORPIPE) != 0)

/* Return skinned version of *NP*s name */
#define skinned_name(NP)         \
   (assert((NP)->n_flags & NAME_SKINNED), \
   ((struct name const*)NP)->n_name)

/* Skin an address according to the RFC 822 interpretation of "host-phrase" */
FL char *      skin(char const *name);

/* Skin *name* and extract the *addr-spec* according to RFC 5322.
 * Store the result in .ag_skinned and also fill in those .ag_ fields that have
 * actually been seen.
 * Return 0 if something good has been parsed, 1 if fun didn't exactly know how
 * to deal with the input, or if that was plain invalid */
FL int         addrspec_with_guts(int doskin, char const *name,
                  struct addrguts *agp);

/* Fetch the real name from an internet mail address field */
FL char *      realname(char const *name);

/* Fetch the sender's name from the passed message.  reptype can be
 * 0 -- get sender's name for display purposes
 * 1 -- get sender's name for reply
 * 2 -- get sender's name for Reply */
FL char *      name1(struct message *mp, int reptype);

/* Trim away all leading Re: etc., return pointer to plain subject.
 * Note it doesn't perform any MIME decoding by itself */
FL char *      subject_re_trim(char *cp);

FL int         msgidcmp(char const *s1, char const *s2);

/* See if the given header field is supposed to be ignored */
FL int         is_ign(char const *field, size_t fieldlen,
                  struct ignoretab igta[2]);

FL int         member(char const *realfield, struct ignoretab *table);

/* Fake Sender for From_ lines if missing, e. g. with POP3 */
FL char const * fakefrom(struct message *mp);

FL char const * fakedate(time_t t);

/* From username Fri Jan  2 20:13:51 2004
 *               |    |    |    |    |
 *               0    5   10   15   20 */
#ifdef HAVE_IMAP_SEARCH
FL time_t      unixtime(char const *from);
#endif

FL time_t      rfctime(char const *date);

FL time_t      combinetime(int year, int month, int day,
                  int hour, int minute, int second);

FL void        substdate(struct message *m);

/* TODO Weird thing that tries to fill in From: and Sender: */
FL void        setup_from_and_sender(struct header *hp);

/* Note: returns 0x1 if both args were NULL */
FL struct name const * check_from_and_sender(struct name const *fromfield,
                        struct name const *senderfield);

#ifdef HAVE_OPENSSL
FL char *      getsender(struct message *m);
#endif

/* Fill in / reedit the desired header fields */
FL int         grab_headers(struct header *hp, enum gfield gflags,
                  int subjfirst);

/* Check wether sep->ss_sexpr (or ->ss_regex) matches any header of mp */
FL bool_t      header_match(struct message *mp, struct search_expr const *sep);

/*
 * imap_search.c
 */

#ifdef HAVE_IMAP_SEARCH
FL enum okay   imap_search(char const *spec, int f);
#endif

/*
 * lex_input.c
 */

/* Print the docstring of `comm', which may be an abbreviation.
 * Return FAL0 if there is no such command */
#ifdef HAVE_DOCSTRINGS
FL bool_t      n_print_comm_docstr(char const *comm);
#endif

/* Interpret user commands.  If stdin is not a tty, print no prompt; return
 * wether last processed command returned error -- this is *only* for main()! */
FL bool_t      n_commands(void);

/* Actual cmd input */

/* Read a complete line of input, with editing if interactive and possible.
 * If prompt is NULL we'll call getprompt() first, if necessary.
 * nl_escape defines wether user can escape newlines via backslash (POSIX).
 * If string is set it is used as the initial line content if in interactive
 * mode, otherwise this argument is ignored for reproducibility.
 * Return number of octets or a value <0 on error.
 * Note: may use the currently `source'd file stream instead of stdin! */
FL int         n_lex_input(char const *prompt, bool_t nl_escape,
                  char **linebuf, size_t *linesize, char const *string
                  SMALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define n_lex_input(A,B,C,D,E) n_lex_input(A,B,C,D,E,__FILE__,__LINE__)
#endif

/* Read a line of input, with editing if interactive and possible, return it
 * savestr()d or NULL in case of errors or if an empty line would be returned.
 * This may only be called from toplevel (not during PS_ROBOT).
 * If prompt is NULL we'll call getprompt() if necessary.
 * If string is set it is used as the initial line content if in interactive
 * mode, otherwise this argument is ignored for reproducibility.
 * If OPT_INTERACTIVE a non-empty return is saved in the history, isgabby */
FL char *      n_lex_input_cp_addhist(char const *prompt, char const *string,
                  bool_t isgabby);

/* Deal with loading of resource files and dealing with a stack of files for
 * the source command */

/* Load a file of user definitions -- this is *only* for main()! */
FL void        n_load(char const *name);

/* "Load" all the -X command line definitions in order -- *only* for main() */
FL void        n_load_Xargs(char const **lines);

/* Pushdown current input file and switch to a new one.  Set the global flag
 * PS_SOURCING so that others will realize that they are no longer reading from
 * a tty (in all probability).
 * The latter won't return failure (TODO should be replaced by "-f FILE") */
FL int         c_source(void *v);
FL int         c_source_if(void *v);

/* Evaluate a complete macro / a single command.  For the former lines will
 * always be free()d, for the latter cmd will always be duplicated internally */
FL bool_t      n_source_macro(char const *name, char **lines);
FL bool_t      n_source_command(char const *cmd);

/* XXX Hack: may we release our (interactive) (terminal) control to a different
 * XXX program, e.g., a $PAGER? */
FL bool_t      n_source_may_yield_control(void);

/*
 * list.c
 */

/* Count the number of arguments in the given string raw list */
FL int         argcount(char **argv);

/* Convert user string of message numbers and store the numbers into vector.
 * Returns the count of messages picked up or -1 on error */
FL int         getmsglist(char *buf, int *vector, int flags);

/* Scan out the list of string arguments according to rm, return -1 on error;
 * res_dat is NULL terminated unless res_size is 0 or error occurred */
FL int         getrawlist(bool_t wysh, char **res_dat, size_t res_size,
                  char const *line, size_t linesize);

/* Find the first message whose flags&m==f and return its message number */
FL int         first(int f, int m);

/* Mark the named message by setting its mark bit */
FL void        mark(int mesg, int f);

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

/* Check wether sep->ss_sexpr (or ->ss_regex) matches mp.  If with_headers is
 * true then the headers will also be searched (as plain text) */
FL bool_t      message_match(struct message *mp, struct search_expr const *sep,
               bool_t with_headers);

/*  */
FL struct message * setdot(struct message *mp);

/* Touch the named message by setting its MTOUCH flag.  Touched messages have
 * the effect of not being sent back to the system mailbox on exit */
FL void        touch(struct message *mp);

/*
 * maildir.c
 */

FL int         maildir_setfile(char const *name, enum fedit_mode fm);

FL bool_t      maildir_quit(void);

FL enum okay   maildir_append(char const *name, FILE *fp, long offset);

FL enum okay   maildir_remove(char const *name);

/*
 * main.c
 */

/* Quit quickly.  In recursed state, return error to just pop the input level */
FL int         c_exit(void *v);

/*
 * memory.c
 */

/* Try to use alloca() for some function-local buffers and data, fall back to
 * smalloc()/free() if not available */

#ifdef HAVE_ALLOCA
# define ac_alloc(n)    HAVE_ALLOCA(n)
# define ac_free(n)     do {UNUSED(n);} while (0)
#else
# define ac_alloc(n)    smalloc(n)
# define ac_free(n)     free(n)
#endif

/* Generic heap memory */

FL void *      smalloc(size_t s SMALLOC_DEBUG_ARGS);
FL void *      srealloc(void *v, size_t s SMALLOC_DEBUG_ARGS);
FL void *      scalloc(size_t nmemb, size_t size SMALLOC_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
FL void        sfree(void *v SMALLOC_DEBUG_ARGS);

/* Called by sreset(), then */
FL void        n_memreset(void);

FL int         c_memtrace(void *v);

/* For immediate debugging purposes, it is possible to check on request */
FL bool_t      n__memcheck(char const *file, int line);

# define smalloc(SZ)             smalloc(SZ, __FILE__, __LINE__)
# define srealloc(P,SZ)          srealloc(P, SZ, __FILE__, __LINE__)
# define scalloc(N,SZ)           scalloc(N, SZ, __FILE__, __LINE__)
# define free(P)                 sfree(P, __FILE__, __LINE__)
# define c_memtrace              c_memtrace
# define n_memcheck()            n__memcheck(__FILE__, __LINE__)
#else
# define n_memreset()            do{}while(0)
#endif

/* String storage, auto-reclaimed after execution level is left */

/* Allocate size more bytes of space and return the address of the first byte
 * to the caller.  An even number of bytes are always allocated so that the
 * space will always be on a word boundary */
FL void *      salloc(size_t size SALLOC_DEBUG_ARGS);
FL void *      csalloc(size_t nmemb, size_t size SALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define salloc(SZ)              salloc(SZ, __FILE__, __LINE__)
# define csalloc(NM,SZ)          csalloc(NM, SZ, __FILE__, __LINE__)
#endif

/* Auto-reclaim string storage; if only_if_relaxed is true then only perform
 * the reset when a srelax_hold() is currently active */
FL void        sreset(bool_t only_if_relaxed);

/* The "problem" with sreset() is that it releases all string storage except
 * what was present once spreserve() had been called; it therefore cannot be
 * called from all that code which yet exists and walks about all the messages
 * in order, e.g. quit(), searches, etc., because, unfortunately, these code
 * paths are reached with new intermediate string dope already in use.
 * Thus such code should take a srelax_hold(), successively call srelax() after
 * a single message has been handled, and finally srelax_rele() (unless it is
 * clear that sreset() occurs anyway) */
FL void        srelax_hold(void);
FL void        srelax_rele(void);
FL void        srelax(void);

/* Make current string storage permanent: new allocs will be auto-reclaimed by
 * sreset().  This is called once only, from within main() */
FL void        spreserve(void);

/* 'sstats' command */
#ifdef HAVE_MEMORY_DEBUG
FL int         c_sstats(void *v);
# define c_sstats                c_sstats
#endif

/*
 * mime.c
 */

/* *charset-7bit*, else CHARSET_7BIT */
FL char const * charset_get_7bit(void);

/* *charset-8bit*, else CHARSET_8BIT */
#ifdef HAVE_ICONV
FL char const * charset_get_8bit(void);
#endif

/* LC_CTYPE:CODESET / *ttycharset*, else *charset-8bit*, else CHARSET_8BIT */
FL char const * charset_get_lc(void);

/* *sendcharsets* .. *charset-8bit* iterator; *a_charset_to_try_first* may be
 * used to prepend a charset to this list (e.g., for *reply-in-same-charset*).
 * The returned boolean indicates charset_iter_is_valid().
 * Without HAVE_ICONV, this "iterates" over charset_get_lc() only */
FL bool_t      charset_iter_reset(char const *a_charset_to_try_first);
FL bool_t      charset_iter_next(void);
FL bool_t      charset_iter_is_valid(void);
FL char const * charset_iter(void);

/* And this is (xxx temporary?) which returns the iterator if that is valid and
 * otherwise either charset_get_8bit() or charset_get_lc() dep. on HAVE_ICONV */
FL char const * charset_iter_or_fallback(void);

FL void        charset_iter_recurse(char *outer_storage[2]); /* TODO LEGACY */
FL void        charset_iter_restore(char *outer_storage[2]); /* TODO LEGACY */

/* Check wether our headers will need MIME conversion */
#ifdef HAVE_ICONV
FL char const * need_hdrconv(struct header *hp);
#endif

/* Convert header fields from RFC 1522 format */
FL void        mime_fromhdr(struct str const *in, struct str *out,
                  enum tdflags flags);

/* Interpret MIME strings in parts of an address field */
FL char *      mime_fromaddr(char const *name);

/* fwrite(3) performing the given MIME conversion */
FL ssize_t     mime_write(char const *ptr, size_t size, FILE *f,
                  enum conversion convert, enum tdflags dflags,
                  struct quoteflt *qf, struct str *rest);
FL ssize_t     xmime_write(char const *ptr, size_t size, /* TODO LEGACY */
                  FILE *f, enum conversion convert, enum tdflags dflags);

/*
 * mime_enc.c
 * Content-Transfer-Encodings as defined in RFC 2045 (and RFC 2047):
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8
 * TODO for now this is pretty mixed up regarding this external interface.
 * TODO in v15.0 CTE will be filter based, and explicit conversion will
 * TODO gain clear error codes
 */

/* Default MIME Content-Transfer-Encoding: as via *encoding* */
FL enum mime_enc mime_enc_target(void);

/* Map from a Content-Transfer-Encoding: header body (which may be NULL) */
FL enum mime_enc mime_enc_from_ctehead(char const *hbody);

/* XXX Try to get rid of that */
FL char const * mime_enc_from_conversion(enum conversion const convert);

/* How many characters of (the complete body) ln need to be quoted.
 * Only MIMEEF_ISHEAD and MIMEEF_ISENCWORD are understood */
FL size_t      mime_enc_mustquote(char const *ln, size_t lnlen,
                  enum mime_enc_flags flags);

/* How much space is necessary to encode len bytes in QP, worst case.
 * Includes room for terminator */
FL size_t      qp_encode_calc_size(size_t len);

/* If flags includes QP_ISHEAD these assume "word" input and use special
 * quoting rules in addition; soft line breaks are not generated.
 * Otherwise complete input lines are assumed and soft line breaks are
 * generated as necessary */
FL struct str * qp_encode(struct str *out, struct str const *in,
                  enum qpflags flags);
#ifdef notyet
FL struct str * qp_encode_cp(struct str *out, char const *cp,
                  enum qpflags flags);
FL struct str * qp_encode_buf(struct str *out, void const *vp, size_t vp_len,
                  enum qpflags flags);
#endif

/* If rest is set then decoding will assume body text input (assumes input
 * represents lines, only create output when input didn't end with soft line
 * break [except it finalizes an encoded CRLF pair]), otherwise it is assumed
 * to decode a header strings and (1) uses special decoding rules and (b)
 * directly produces output.
 * The buffers of out and possibly rest will be managed via srealloc().
 * Returns OKAY. XXX or STOP on error (in which case out is set to an error
 * XXX message); caller is responsible to free buffers */
FL int         qp_decode(struct str *out, struct str const *in,
                  struct str *rest);

/* How much space is necessary to encode len bytes in Base64, worst case.
 * Includes room for (CR/LF/CRLF and) terminator */
FL size_t      b64_encode_calc_size(size_t len);

/* Note these simply convert all the input (if possible), including the
 * insertion of NL sequences if B64_CRLF or B64_LF is set (and multiple thereof
 * if B64_MULTILINE is set).
 * Thus, in the B64_BUF case, better call b64_encode_calc_size() first */
FL struct str * b64_encode(struct str *out, struct str const *in,
                  enum b64flags flags);
FL struct str * b64_encode_buf(struct str *out, void const *vp, size_t vp_len,
                  enum b64flags flags);
#ifdef HAVE_SMTP
FL struct str * b64_encode_cp(struct str *out, char const *cp,
                  enum b64flags flags);
#endif

/* If rest is set then decoding will assume text input.
 * The buffers of out and possibly rest will be managed via srealloc().
 * Returns OKAY or STOP on error (in which case out is set to an error
 * message); caller is responsible to free buffers.
 * XXX STOP is effectively not returned for text input (rest!=NULL), instead
 * XXX replacement characters are produced for invalid data */
FL int         b64_decode(struct str *out, struct str const *in,
                  struct str *rest);

/*
 * mime_param.c
 */

/* Get a mime style parameter from a header body */
FL char *      mime_param_get(char const *param, char const *headerbody);

/* Format parameter name to have value, salloc() it or NULL (error) in result.
 * 0 on error, 1 or -1 on success: the latter if result contains \n newlines,
 * which it will if the created param requires more than MIME_LINELEN bytes;
 * there is never a trailing newline character */
/* TODO mime_param_create() should return a StrList<> or something.
 * TODO in fact it should take a HeaderField* and append a HeaderFieldParam*! */
FL si8_t       mime_param_create(struct str *result, char const *name,
                  char const *value);

/* Get the boundary out of a Content-Type: multipart/xyz header field, return
 * salloc()ed copy of it; store strlen() in *len if set */
FL char *      mime_param_boundary_get(char const *headerbody, size_t *len);

/* Create a salloc()ed MIME boundary */
FL char *      mime_param_boundary_create(void);

/*
 * mime_parse.c
 */

/* Create MIME part object tree for and of mp */
FL struct mimepart * mime_parse_msg(struct message *mp,
                        enum mime_parse_flags mpf);

/*
 * mime_types.c
 */

/* `(un)?mimetype' commands */
FL int         c_mimetype(void *v);
FL int         c_unmimetype(void *v);

/* Return a Content-Type matching the name, or NULL if none could be found */
FL char *      mime_type_classify_filename(char const *name);

/* Classify content of *fp* as necessary and fill in arguments; **charset* is
 * left alone unless it's non-NULL */
FL enum conversion mime_type_classify_file(FILE *fp, char const **contenttype,
                     char const **charset, int *do_iconv);

/* Dependend on *mime-counter-evidence* mpp->m_ct_type_usr_ovwr will be set,
 * but otherwise mpp is const */
FL enum mimecontent mime_type_classify_part(struct mimepart *mpp);

/* Query handler for a part, return the plain type (& MIME_HDL_TYPE_MASK).
 * mhp is anyway initialized (mh_flags, mh_msg) */
FL enum mime_handler_flags mime_type_handler(struct mime_handler *mhp,
                              struct mimepart const *mpp,
                              enum sendaction action);

/*
 * nam_a_grp.c
 */

/* Allocate a single element of a name list, initialize its name field to the
 * passed name and return it */
FL struct name * nalloc(char *str, enum gfield ntype);

/* Like nalloc(), but initialize from content of np */
FL struct name * ndup(struct name *np, enum gfield ntype);

/* Concatenate the two passed name lists, return the result */
FL struct name * cat(struct name *n1, struct name *n2);

/* Duplicate np */
FL struct name * namelist_dup(struct name const *np, enum gfield ntype);

/* Determine the number of undeleted elements in a name list and return it;
 * the latter also doesn't count file and pipe addressees in addition */
FL ui32_t      count(struct name const *np);
FL ui32_t      count_nonlocal(struct name const *np);

/* Extract a list of names from a line, and make a list of names from it.
 * Return the list or NULL if none found */
FL struct name * extract(char const *line, enum gfield ntype);

/* Like extract() unless line contains anyof ",\"\\(<|", in which case
 * comma-separated list extraction is used instead */
FL struct name * lextract(char const *line, enum gfield ntype);

/* Turn a list of names into a string of the same names */
FL char *      detract(struct name *np, enum gfield ntype);

/* Get a lextract() list via n_lex_input_cp_addhist(), reassigning to *np* */
FL struct name * grab_names(char const *field, struct name *np, int comma,
                     enum gfield gflags);

/* Check wether n1 n2 share the domain name */
FL bool_t      name_is_same_domain(struct name const *n1,
                  struct name const *n2);

/* Check all addresses in np and delete invalid ones; if set_on_error is not
 * NULL it'll be set to TRU1 for error or -1 for "hard fail" error */
FL struct name * checkaddrs(struct name *np, enum expand_addr_check_mode eacm,
                  si8_t *set_on_error);

/* Vaporise all duplicate addresses in hp (.h_(to|cc|bcc)) so that an address
 * that "first" occurs in To: is solely in there, ditto Cc:, after expanding
 * aliases etc.  eacm and set_on_error are passed to checkaddrs(), metoo is
 * passed to usermap().  After updating hp to the new state this returns
 * a flat list of all addressees, which may be NULL */
FL struct name * namelist_vaporise_head(struct header *hp,
                  enum expand_addr_check_mode eacm, bool_t metoo,
                  si8_t *set_on_error);

/* Map all of the aliased users in the invoker's mailrc file and insert them
 * into the list */
FL struct name * usermap(struct name *names, bool_t force_metoo);

/* Remove all of the duplicates from the passed name list by insertion sorting
 * them, then checking for dups.  Return the head of the new list */
FL struct name * elide(struct name *names);

/* `alternates' deal with the list of alternate names */
FL int         c_alternates(void *v);

FL struct name * delete_alternates(struct name *np);

FL int         is_myname(char const *name);

/* `(un)?alias' */
FL int         c_alias(void *v);
FL int         c_unalias(void *v);

/* `(un)?ml(ist|subscribe)', and a check wether a name is a (wanted) list */
FL int         c_mlist(void *v);
FL int         c_unmlist(void *v);
FL int         c_mlsubscribe(void *v);
FL int         c_unmlsubscribe(void *v);

FL enum mlist_state is_mlist(char const *name, bool_t subscribed_only);

/* `(un)?shortcut', and check if str is one, return expansion or NULL */
FL int         c_shortcut(void *v);
FL int         c_unshortcut(void *v);

FL char const * shortcut_expand(char const *str);

/* `(un)?customhdr'.
 * Query a list of all currently defined custom headers (salloc()ed) */
FL int         c_customhdr(void *v);
FL int         c_uncustomhdr(void *v);

FL struct n_header_field * n_customhdr_query(void);

/*
 * openssl.c
 */

#ifdef HAVE_OPENSSL
/*  */
FL enum okay   ssl_open(struct url const *urlp, struct sock *sp);

/*  */
FL void        ssl_gen_err(char const *fmt, ...);

/*  */
FL int         c_verify(void *vp);

/*  */
FL FILE *      smime_sign(FILE *ip, char const *addr);

/*  */
FL FILE *      smime_encrypt(FILE *ip, char const *certfile, char const *to);

FL struct message * smime_decrypt(struct message *m, char const *to,
                     char const *cc, int signcall);

/*  */
FL enum okay   smime_certsave(struct message *m, int n, FILE *op);

#else /* HAVE_OPENSSL */
# define c_verify                c_cmdnotsupp
#endif

/*
 * path.c
 */

/* Test to see if the passed file name is a directory, return true if it is */
FL bool_t      is_dir(char const *name);

/* Recursively create a directory */
FL bool_t      n_path_mkdir(char const *name);

/* Delete a file, but only if the file is a plain file; return FAL0 on system
 * error and TRUM1 if name is not a plain file, return TRU1 on success */
FL bool_t      n_path_rm(char const *name);

/* A get-wd..restore-wd approach */
FL enum okay   cwget(struct cw *cw);
FL enum okay   cwret(struct cw *cw);
FL void        cwrelse(struct cw *cw);

/*
 * pop3.c
 */

#ifdef HAVE_POP3
/*  */
FL enum okay   pop3_noop(void);

/*  */
FL int         pop3_setfile(char const *server, enum fedit_mode fm);

/*  */
FL enum okay   pop3_header(struct message *m);

/*  */
FL enum okay   pop3_body(struct message *m);

/*  */
FL bool_t      pop3_quit(void);
#endif /* HAVE_POP3 */

/*
 * popen.c
 * Subprocesses, popen, but also file handling with registering
 */

/* For program startup in main.c: initialize process manager */
FL void        command_manager_start(void);

/* Notes: OF_CLOEXEC is implied in oflags, xflags may be NULL */
FL FILE *      safe_fopen(char const *file, char const *oflags, int *xflags);

/* Notes: OF_CLOEXEC|OF_REGISTER are implied in oflags!
 * Exception is Fdopen() if nocloexec is TRU1, but otherwise even for it the fd
 * creator has to take appropriate steps in order to ensure this is true! */
FL FILE *      Fopen(char const *file, char const *oflags);
FL FILE *      Fdopen(int fd, char const *oflags, bool_t nocloexec);

FL int         Fclose(FILE *fp);

/* Open file according to oflags (see popen.c).  Handles compressed files */
FL FILE *      Zopen(char const *file, char const *oflags);

/* Create a temporary file in tempdir, use namehint for its name (prefix
 * unless OF_SUFFIX is set, in which case namehint is an extension that MUST be
 * part of the resulting filename, otherwise Ftmp() will fail), store the
 * unique name in fn (unless OF_UNLINK is set in oflags), and return a stdio
 * FILE pointer with access oflags.  OF_CLOEXEC is implied in oflags.
 * One of OF_WRONLY and OF_RDWR must be set.  Mode of 0600 is implied */
FL FILE *      Ftmp(char **fn, char const *namehint, enum oflags oflags);

/* If OF_HOLDSIGS was set when calling Ftmp(), then hold_all_sigs() had been
 * called: call this to unlink(2) and free *fn and to rele_all_sigs() */
FL void        Ftmp_release(char **fn);

/* Free the resources associated with the given filename.  To be called after
 * unlink() */
FL void        Ftmp_free(char **fn);

/* Create a pipe and ensure CLOEXEC bit is set in both descriptors */
FL bool_t      pipe_cloexec(int fd[2]);

/*
 * env_addon may be NULL, otherwise it is expected to be a NULL terminated
 * array of "K=V" strings to be placed into the childs environment.
 * If cmd==(char*)-1 then shell is indeed expected to be a PTF :P that will be
 * called from within the child process */
FL FILE *      Popen(char const *cmd, char const *mode, char const *shell,
                  char const **env_addon, int newfd1);
FL bool_t      Pclose(FILE *fp, bool_t dowait);

/* In OPT_INTERACTIVE, we want to go over $PAGER.
 * These are specialized version of Popen()/Pclose() which also encapsulate
 * error message printing, terminal handling etc. additionally */
FL FILE *      n_pager_open(void);
FL bool_t      n_pager_close(FILE *fp);

/*  */
FL void        close_all_files(void);

/* Fork a child process, enable use of the *child() series below */
FL int         fork_child(void);

/* Run a command without a shell, with optional arguments and splicing of stdin
 * and stdout.  FDs may also be COMMAND_FD_NULL and COMMAND_FD_PASS, meaning to
 * redirect from/to /dev/null or pass through our own set of FDs; in the
 * latter case terminal capabilities are saved/restored if possible.
 * The command name can be a sequence of words.
 * Signals must be handled by the caller.  "Mask" contains the signals to
 * ignore in the new process.  SIGINT is enabled unless it's in the mask.
 * env_addon may be NULL, otherwise it is expected to be a NULL terminated
 * array of "K=V" strings to be placed into the childs environment */
FL int         run_command(char const *cmd, sigset_t *mask, int infd,
                  int outfd, char const *a0, char const *a1, char const *a2,
                  char const **env_addon);

/* Like run_command, but don't wait for the command to finish.
 * Also it is usually an error to use COMMAND_FD_PASS for this one */
FL int         start_command(char const *cmd, sigset_t *mask, int infd,
                  int outfd, char const *a0, char const *a1, char const *a2,
                  char const **env_addon);

/* In-child process */
FL void        prepare_child(sigset_t *nset, int infd, int outfd);

/* Mark a child as don't care - pid */
FL void        free_child(int pid);

/* Wait for pid, return wether we've had a normal EXIT_SUCCESS exit.
 * If wait_status is set, set it to the reported waitpid(2) wait status */
FL bool_t      wait_child(int pid, int *wait_status);

/*
 * quit.c
 */

/* Save all of the undetermined messages at the top of "mbox".  Save all
 * untouched messages back in the system mailbox.  Remove the system mailbox,
 * if none saved there */
FL bool_t      quit(void);

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
 * Return -1 on error.  Adjust the status: field if need be.  If doign is
 * given, suppress ignored header fields.  prefix is a string to prepend to
 * each output line.   action = data destination
 * (SEND_MBOX,_TOFILE,_TODISP,_QUOTE,_DECRYPT).  stats[0] is line count,
 * stats[1] is character count.  stats may be NULL.  Note that stats[0] is
 * valid for SEND_MBOX only */
FL int         sendmp(struct message *mp, FILE *obuf, struct ignoretab *doign,
                  char const *prefix, enum sendaction action, ui64_t *stats);

/*
 * sendout.c
 */

/* Interface between the argument list and the mail1 routine which does all the
 * dirty work */
FL int         mail(struct name *to, struct name *cc, struct name *bcc,
                  char *subject, struct attachment *attach, char *quotefile,
                  int recipient_record);

/* `mail' and `Mail' commands, respectively */
FL int         c_sendmail(void *v);
FL int         c_Sendmail(void *v);

/* Mail a message on standard input to the people indicated in the passed
 * header.  (Internal interface) */
FL enum okay   mail1(struct header *hp, int printheaders,
                  struct message *quote, char *quotefile, int recipient_record,
                  int doprefix);

/* Create a Date: header field.
 * We compare the localtime() and gmtime() results to get the timezone, because
 * numeric timezones are easier to read and because $TZ isn't always set */
FL int         mkdate(FILE *fo, char const *field);

/* Dump the to, subject, cc header on the passed file buffer.
 * nosend_msg tells us not to dig to deep but to instead go for compose mode or
 * editing a message (yet we're stupid and cannot do it any better) - if it is
 * TRUM1 then we're really in compose mode and will produce some fields for
 * easier filling in */
FL int         puthead(bool_t nosend_msg, struct header *hp, FILE *fo,
                  enum gfield w, enum sendaction action,
                  enum conversion convert, char const *contenttype,
                  char const *charset);

/*  */
FL enum okay   resend_msg(struct message *mp, struct name *to, int add_resent);

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
 * If FEXP_NSHELL is set you possibly want to call fexpand_nshell_quote(),
 * a poor man's vis(3), on name before calling this (and showing the user).
 * Returns the file name as an auto-reclaimed string */
FL char *      fexpand(char const *name, enum fexp_mode fexpm);

#define expand(N)                fexpand(N, FEXP_FULL)   /* XXX obsolete */
#define file_expand(N)           fexpand(N, FEXP_LOCAL)  /* XXX obsolete */

/* A poor man's vis(3) for only backslash escaping as for FEXP_NSHELL.
 * Returns the (possibly adjusted) buffer in auto-reclaimed storage */
FL char *      fexpand_nshell_quote(char const *name);

/* (Try to) Expand ^~/? and ^~USER/? constructs.
 * Returns the completely resolved (maybe empty or identical to input)
 * salloc()ed string */
FL char *      n_shell_expand_tilde(char const *s, bool_t *err_or_null);

/* (Try to) Expand any shell variable in s, allowing backslash escaping
 * (of any following character) with bsescape.
 * Returns the completely resolved (maybe empty) salloc()ed string.
 * Logs on error */
FL char *      n_shell_expand_var(char const *s, bool_t bsescape,
                  bool_t *err_or_null);

/* Check wether *s is an escape sequence, expand it as necessary.
 * Returns the expanded sequence or 0 if **s is NUL or PROMPT_STOP if it is \c.
 * *s is advanced to after the expanded sequence (as possible).
 * If use_prompt_extensions is set, an enum prompt_exp may be returned */
FL int         n_shell_expand_escape(char const **s,
                  bool_t use_prompt_extensions);

/* Parse the next shell token from input (->s and ->l are adjusted to the
 * remains, data is constant beside that; ->s may be NULL if ->l is 0, if ->l
 * EQ UIZ_MAX strlen(->s) is used) and _append_ the resulting output to store.
 * dolog states wether error logging shall be performed, TRUM1 -> OPT_D_V */
FL enum n_shexp_state n_shell_parse_token(struct n_string *store,
                        struct str *input, bool_t dolog);

/* Treat input as list of shell tokens (->s and ->l are adjusted to the
 * remains, except for _STOP and errors, data is constant beside that; ->s may
 * be NULL if ->l is 0, if ->l EQ UIZ_MAX strlen(->s) is used), optionally also
 * separated with commas (CSV); find and return the next entry in store,
 * trimming surrounding whitespace.
 * If ignore_empty is set empty entries are started over.
 * dolog states wether error logging shall be performed, TRUM1 -> OPT_D_V */
FL enum n_shexp_state n_shell_sep(struct n_string *store, struct str *input,
                        bool_t ignore_empty, bool_t dolog);

/* Quote input in a way that can, in theory, be fed into parse_token() again.
 * ->s may be NULL if ->l is 0, if ->l EQ UIZ_MAX strlen(->s) is used.
 * Resulting output is _appended_ to store.
 * TODO Note: last resort, since \u and $ expansions etc. are necessarily
 * TODO already expanded and can thus not be reverted, but ALL we have */
FL struct n_string *n_shell_quote(struct n_string *store,
                     struct str const *input);
FL char *      n_shell_quote_cp(char const *cp);

/*
 * signal.c
 */

FL void        n_raise(int signo);

/* Provide BSD-like signal() on all (POSIX) systems */
FL sighandler_type safe_signal(int signum, sighandler_type handler);

/* Hold *all* signals but SIGCHLD, and release that total block again */
FL void        hold_all_sigs(void);
FL void        rele_all_sigs(void);

/* Hold HUP/QUIT/INT */
FL void        hold_sigs(void);
FL void        rele_sigs(void);

/* Call _ENTER_SWITCH() with the according flags, it'll take care for the rest
 * and also set the jump buffer - it returns 0 if anything went fine and
 * a signal number if a jump occurred, in which case all handlers requested in
 * flags are temporarily SIG_IGN.
 * _cleanup_ping() informs the condome that no jumps etc. shall be performed
 * until _leave() is called in the following -- to be (optionally) called right
 * before the local jump label is reached which is jumped to after a long jump
 * occurred, straight code flow provided, e.g., to avoid destructors to be
 * called twice.  _leave() must always be called last, reraise_flags will be
 * used to decide how signal handling has to continue
 */
#define n_SIGMAN_ENTER_SWITCH(S,F) do{\
   int __x__;\
   hold_sigs();\
   if(sigsetjmp((S)->sm_jump, 1))\
      __x__ = -1;\
   else\
      __x__ = F;\
   n__sigman_enter(S, __x__);\
}while(0); switch((S)->sm_signo)
FL int         n__sigman_enter(struct n_sigman *self, int flags);
FL void        n_sigman_cleanup_ping(struct n_sigman *self);
FL void        n_sigman_leave(struct n_sigman *self, enum n_sigman_flags flags);

/* Pending signal or 0? */
FL int         n_sigman_peek(void);
FL void        n_sigman_consume(void);

/* Not-Yet-Dead debug information (handler installation in main.c) */
#if defined HAVE_DEBUG || defined HAVE_DEVEL
FL void        _nyd_chirp(ui8_t act, char const *file, ui32_t line,
                  char const *fun);
FL void        _nyd_oncrash(int signo);

# define HAVE_NYD
# define NYD_ENTER               _nyd_chirp(1, __FILE__, __LINE__, __FUN__)
# define NYD_LEAVE               _nyd_chirp(2, __FILE__, __LINE__, __FUN__)
# define NYD                     _nyd_chirp(0, __FILE__, __LINE__, __FUN__)
# define NYD_X                   _nyd_chirp(0, __FILE__, __LINE__, __FUN__)
# ifdef HAVE_NYD2
#  define NYD2_ENTER             _nyd_chirp(1, __FILE__, __LINE__, __FUN__)
#  define NYD2_LEAVE             _nyd_chirp(2, __FILE__, __LINE__, __FUN__)
#  define NYD2                   _nyd_chirp(0, __FILE__, __LINE__, __FUN__)
# endif
#else
# undef HAVE_NYD
#endif
#ifndef NYD
# define NYD_ENTER               do {} while (0)
# define NYD_LEAVE               do {} while (0)
# define NYD                     do {} while (0)
# define NYD_X                   do {} while (0) /* XXX LEGACY */
#endif
#ifndef NYD2
# define NYD2_ENTER              do {} while (0)
# define NYD2_LEAVE              do {} while (0)
# define NYD2                    do {} while (0)
#endif

/*
 * smtp.c
 */

#ifdef HAVE_SMTP
/* Send a message via SMTP */
FL bool_t      smtp_mta(struct sendbundle *sbp);
#endif

/*
 * socket.c
 */

#ifdef HAVE_SOCKETS
FL bool_t      sopen(struct sock *sp, struct url *urlp);
FL int         sclose(struct sock *sp);
FL enum okay   swrite(struct sock *sp, char const *data);
FL enum okay   swrite1(struct sock *sp, char const *data, int sz,
                  int use_buffer);

/*  */
FL int         sgetline(char **line, size_t *linesize, size_t *linelen,
                  struct sock *sp SMALLOC_DEBUG_ARGS);
# ifdef HAVE_MEMORY_DEBUG
#  define sgetline(A,B,C,D)      sgetline(A, B, C, D, __FILE__, __LINE__)
# endif
#endif

/*
 * spam.c
 */

#ifdef HAVE_SPAM
/* Direct mappings of the various spam* commands */
FL int         c_spam_clear(void *v);
FL int         c_spam_set(void *v);
FL int         c_spam_forget(void *v);
FL int         c_spam_ham(void *v);
FL int         c_spam_rate(void *v);
FL int         c_spam_spam(void *v);
#else
# define c_spam_clear            c_cmdnotsupp
# define c_spam_set              c_cmdnotsupp
# define c_spam_forget           c_cmdnotsupp
# define c_spam_ham              c_cmdnotsupp
# define c_spam_rate             c_cmdnotsupp
# define c_spam_spam             c_cmdnotsupp
#endif

/*
 * ssl.c
 */

#ifdef HAVE_SSL
/*  */
FL void        ssl_set_verify_level(struct url const *urlp);

/*  */
FL enum okay   ssl_verify_decide(void);

/*  */
FL enum okay   smime_split(FILE *ip, FILE **hp, FILE **bp, long xcount,
                  int keep);

/* */
FL FILE *      smime_sign_assemble(FILE *hp, FILE *bp, FILE *sp,
                  char const *message_digest);

/*  */
FL FILE *      smime_encrypt_assemble(FILE *hp, FILE *yp);

/*  */
FL struct message * smime_decrypt_assemble(struct message *m, FILE *hp,
                     FILE *bp);

/*  */
FL int         c_certsave(void *v);

/*  */
FL enum okay   rfc2595_hostname_match(char const *host, char const *pattern);
#else /* HAVE_SSL */
# define c_certsave              c_cmdnotsupp
#endif

/*
 * strings.c
 */

/* Return a pointer to a dynamic copy of the argument */
FL char *      savestr(char const *str SALLOC_DEBUG_ARGS);
FL char *      savestrbuf(char const *sbuf, size_t sbuf_len SALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define savestr(CP)             savestr(CP, __FILE__, __LINE__)
# define savestrbuf(CBP,CBL)     savestrbuf(CBP, CBL, __FILE__, __LINE__)
#endif

/* Concatenate cp2 onto cp1 (if not NULL), separated by sep (if not '\0') */
FL char *      savecatsep(char const *cp1, char sep, char const *cp2
                  SALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define savecatsep(S1,SEP,S2)   savecatsep(S1, SEP, S2, __FILE__, __LINE__)
#endif

/* Make copy of argument incorporating old one, if set, separated by space */
#define save2str(S,O)            savecatsep(O, ' ', S)

/* strcat */
#define savecat(S1,S2)           savecatsep(S1, '\0', S2)

/* Create duplicate, lowercasing all characters along the way */
FL char *      i_strdup(char const *src SALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define i_strdup(CP)            i_strdup(CP, __FILE__, __LINE__)
#endif

/*  */
FL struct str * str_concat_csvl(struct str *self, ...);

/*  */
FL struct str * str_concat_cpa(struct str *self, char const * const *cpa,
                  char const *sep_o_null SALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define str_concat_cpa(S,A,N)   str_concat_cpa(S, A, N, __FILE__, __LINE__)
#endif

/* Plain char* support, not auto-reclaimed (unless noted) */

/* Are any of the characters in the two strings the same? */
FL int         anyof(char const *s1, char const *s2);

/* Treat *iolist as a sep separated list of strings; find and return the
 * next entry, trimming surrounding whitespace, and point *iolist to the next
 * entry or to NULL if no more entries are contained.  If ignore_empty is
 * set empty entries are started over.
 * See n_shell_sep() for the new way that supports sh(1) quoting.
 * strescsep will assert that sep is not NULL, and allows escaping of the
 * separator character with a backslash.
 * Return NULL or an entry */
FL char *      n_strsep(char **iolist, char sep, bool_t ignore_empty);
FL char *      n_strescsep(char **iolist, char sep, bool_t ignore_empty);

/* Copy a string, lowercasing it as we go; *size* is buffer size of *dest*;
 * *dest* will always be terminated unless *size* is 0 */
FL void        i_strcpy(char *dest, char const *src, size_t size);

/* Is *as1* a valid prefix of *as2*? */
FL int         is_prefix(char const *as1, char const *as2);

/* Backslash quote (" and \) v'alue, and return salloc()ed result */
FL char *      string_quote(char const *v);

/* Get (and isolate) the last, possibly quoted part of linebuf, set *needs_list
 * to indicate wether getmsglist() et al need to be called to collect
 * additional args that remain in linebuf.  If strip is true possibly
 * surrounding quote characters are removed.  Return NULL on "error" */
FL char *      laststring(char *linebuf, bool_t *needs_list, bool_t strip);

/* Convert a string to lowercase, in-place and with multibyte-aware */
FL void        makelow(char *cp);

/* Is *sub* a substring of *str*, case-insensitive and multibyte-aware? */
FL bool_t      substr(char const *str, char const *sub);

/*  */
FL char *      sstpcpy(char *dst, char const *src);
FL char *      sstrdup(char const *cp SMALLOC_DEBUG_ARGS);
FL char *      sbufdup(char const *cp, size_t len SMALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define sstrdup(CP)             sstrdup(CP, __FILE__, __LINE__)
# define sbufdup(CP,L)           sbufdup(CP, L, __FILE__, __LINE__)
#endif

/* Copy at most dstsize bytes of src to dst and return string length.
 * Returns -E2BIG if dst is not large enough; dst will always be terminated
 * unless dstsize was 0 on entry */
FL ssize_t     n_strscpy(char *dst, char const *src, size_t dstsize);

/* Case-independent ASCII comparisons */
FL int         asccasecmp(char const *s1, char const *s2);
FL int         ascncasecmp(char const *s1, char const *s2, size_t sz);

/* Case-independent ASCII string find s2 in s1, return it or NULL */
FL char const *asccasestr(char const *s1, char const *s2);

/* Case-independent ASCII check wether as2 is the initial substring of as1 */
FL bool_t      is_asccaseprefix(char const *as1, char const *as2);

/* struct str related support funs TODO _cp->_cs! */

/* *self->s* is srealloc()ed */
#define n_str_dup(S, T)          n_str_assign_buf((S), (T)->s, (T)->l)

/* *self->s* is srealloc()ed; if buflen==UIZ_MAX strlen() is called unless buf
 * is NULL; buf may be NULL if buflen is 0 */
FL struct str * n_str_assign_buf(struct str *self,
                  char const *buf, uiz_t buflen SMALLOC_DEBUG_ARGS);
#define n_str_assign(S, T)       n_str_assign_buf(S, (T)->s, (T)->l)
#define n_str_assign_cp(S, CP)   n_str_assign_buf(S, CP, UIZ_MAX)

/* *self->s* is srealloc()ed, *self->l* incremented; if buflen==UIZ_MAX
 * strlen() is called unless buf is NULL; buf may be NULL if buflen is 0 */
FL struct str * n_str_add_buf(struct str *self, char const *buf, uiz_t buflen
                  SMALLOC_DEBUG_ARGS);
#define n_str_add(S, T)          n_str_add_buf(S, (T)->s, (T)->l)
#define n_str_add_cp(S, CP)      n_str_add_buf(S, CP, UIZ_MAX)

#ifdef HAVE_MEMORY_DEBUG
# define n_str_assign_buf(S,B,BL) n_str_assign_buf(S, B, BL, __FILE__, __LINE__)
# define n_str_add_buf(S,B,BL)   n_str_add_buf(S, B, BL, __FILE__, __LINE__)
#endif

/* struct n_string
 * May have NULL buffer, may contain embedded NULs */

/* Lifetime */
#define n_string_creat(S) \
   ((S)->s_dat = NULL, (S)->s_len = (S)->s_auto = (S)->s_size = 0, (S))
#define n_string_creat_auto(S) \
   ((S)->s_dat = NULL, (S)->s_len = (S)->s_size = 0, (S)->s_auto = TRU1, (S))
#define n_string_gut(S) ((S)->s_size != 0 ? (void)n_string_clear(S) : (void)0)

/* Truncate to size, which must be LE current length */
#define n_string_trunc(S,L)      ((S)->s_len = (L), (S))

/* Release buffer ownership */
#define n_string_drop_ownership(S) \
   ((S)->s_dat = NULL, (S)->s_len = (S)->s_size = 0, (S))

/* Release all memory */
FL struct n_string *n_string_clear(struct n_string *self SMALLOC_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
# define n_string_clear(S) \
   ((S)->s_size != 0 ? (n_string_clear)(S, __FILE__, __LINE__) : (S))
#else
# define n_string_clear(S)       ((S)->s_size != 0 ? (n_string_clear)(S) : (S))
#endif

/* Reserve room for noof additional bytes */
FL struct n_string *n_string_reserve(struct n_string *self, size_t noof
                     SMALLOC_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
# define n_string_reserve(S,N)   (n_string_reserve)(S, N, __FILE__, __LINE__)
#endif

/* */
FL struct n_string *n_string_push_buf(struct n_string *self, char const *buf,
                     size_t buflen SMALLOC_DEBUG_ARGS);
#define n_string_push(S, T)       n_string_push_buf(S, (T)->s_len, (T)->s_dat)
#define n_string_push_cp(S,CP)    n_string_push_buf(S, CP, UIZ_MAX)
FL struct n_string *n_string_push_c(struct n_string *self, char c
                     SMALLOC_DEBUG_ARGS);

#define n_string_assign(S,T)     ((S)->s_len = 0, n_string_push(S, T))
#define n_string_assign_cp(S,CP) ((S)->s_len = 0, n_string_push_cp(S, CP))
#define n_string_assign_buf(S,B,BL) \
   ((S)->s_len = 0, n_string_push_buf(S, B, BL))

#ifdef HAVE_MEMORY_DEBUG
# define n_string_push_buf(S,B,BL) \
   n_string_push_buf(S, B, BL, __FILE__, __LINE__)
# define n_string_push_c(S,C)    n_string_push_c(S, C, __FILE__, __LINE__)
#endif

/* */
FL struct n_string *n_string_unshift_buf(struct n_string *self, char const *buf,
                     size_t buflen SMALLOC_DEBUG_ARGS);
#define n_string_unshift(S, T) \
   n_string_unshift_buf(S, (T)->s_len, (T)->s_dat)
#define n_string_unshift_cp(S,CP) \
      n_string_unshift_buf(S, CP, UIZ_MAX)
FL struct n_string *n_string_unshift_c(struct n_string *self, char c
                     SMALLOC_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
# define n_string_unshift_buf(S,B,BL) \
   n_string_unshift_buf(S, B, BL, __FILE__, __LINE__)
# define n_string_unshift_c(S,C) n_string_unshift_c(S, C, __FILE__, __LINE__)
#endif

/* Ensure self has a - NUL terminated - buffer, and return that.
 * The latter may return the pointer to an internal empty RODATA instead */
FL char *      n_string_cp(struct n_string *self SMALLOC_DEBUG_ARGS);
FL char const *n_string_cp_const(struct n_string const *self);

#ifdef HAVE_MEMORY_DEBUG
# define n_string_cp(S)          n_string_cp(S, __FILE__, __LINE__)
#endif

#ifdef HAVE_INLINE
n_INLINE struct n_string *
(n_string_creat)(struct n_string *self){
   return n_string_creat(self);
}
# undef n_string_creat

n_INLINE struct n_string *
(n_string_creat_auto)(struct n_string *self){
   return n_string_creat_auto(self);
}
# undef n_string_creat_auto

n_INLINE void
(n_string_gut)(struct n_string *self){
   n_string_gut(self);
}
# undef n_string_gut

n_INLINE struct n_string *
(n_string_trunc)(struct n_string *self, size_t l){
   return n_string_trunc(self, l);
}
# undef n_string_trunc

n_INLINE struct n_string *
(n_string_drop_ownership)(struct n_string *self){
   return n_string_drop_ownership(self);
}
# undef n_string_drop_ownership
#endif /* HAVE_INLINE */

/* UTF-8 stuff */

/* ..and update arguments to point after range; returns UI32_MAX on error, in
 * which case the arguments will have been stepped one byte */
#ifdef HAVE_NATCH_CHAR
FL ui32_t      n_utf8_to_utf32(char const **bdat, size_t *blen);
#endif

/* buf must be large enough also for NUL, it's new length will be returned */
#ifdef HAVE_FILTER_HTML_TAGSOUP
FL size_t      n_utf32_to_utf8(ui32_t c, char *buf);
#endif

/* Our iconv(3) wrappers */

#ifdef HAVE_ICONV
FL iconv_t     n_iconv_open(char const *tocode, char const *fromcode);
/* If *cd* == *iconvd*, assigns -1 to the latter */
FL void        n_iconv_close(iconv_t cd);

/* Reset encoding state */
FL void        n_iconv_reset(iconv_t cd);

/* iconv(3), but return *errno* or 0; *skipilseq* forces step over invalid byte
 * sequences; likewise iconv_str(), but which auto-grows on E2BIG errors; *in*
 * and *in_rest_or_null* may be the same object.
 * Note: EINVAL (incomplete sequence at end of input) is NOT handled, so the
 * replacement character must be added manually if that happens at EOF! */
FL int         n_iconv_buf(iconv_t cd, char const **inb, size_t *inbleft,
                  char **outb, size_t *outbleft, bool_t skipilseq);
FL int         n_iconv_str(iconv_t icp, struct str *out, struct str const *in,
                  struct str *in_rest_or_null, bool_t skipilseq);
#endif

/*
 * termcap.c
 * This is a little bit hairy since it provides some stuff even if HAVE_TERMCAP
 * is false due to encapsulation desire
 */

#ifdef n_HAVE_TCAP
/* termcap(3) / xy lifetime handling -- only called if we're OPT_INTERACTIVE
 * but not doing something in OPT_QUICKRUN_MASK */
FL void        n_termcap_init(void);
FL void        n_termcap_destroy(void);

/* enter_ca_mode / enable keypad (both if possible).
 * TODO When complete is not set we won't enter_ca_mode, for example: we don't
 * TODO want a complete screen clearance after $PAGER returned after displaying
 * TODO a mail, because otherwise the screen would look differently for normal
 * TODO stdout display messages.  Etc. */
# ifdef HAVE_TERMCAP
FL void        n_termcap_resume(bool_t complete);
FL void        n_termcap_suspend(bool_t complete);

#  define n_TERMCAP_RESUME(CPL)  n_termcap_resume(CPL)
#  define n_TERMCAP_SUSPEND(CPL) n_termcap_suspend(CPL)
# else
#  define n_TERMCAP_RESUME(CPL)
#  define n_TERMCAP_SUSPEND(CPL)
# endif

/* Command multiplexer, returns FAL0 on I/O error, TRU1 on success and TRUM1
 * for commands which are not available and have no builtin fallback.
 * For query options the return represents a true value and -1 error.
 * Will return FAL0 directly unless we've been initialized.
 * By convention unused argument slots are given as -1 */
FL ssize_t     n_termcap_cmd(enum n_termcap_cmd cmd, ssize_t a1, ssize_t a2);
# define n_termcap_cmdx(CMD)     n_termcap_cmd(CMD, -1, -1)

/* Query multiplexer.  If query is n__TERMCAP_QUERY_MAX then
 * tvp->tv_data.tvd_string must contain the name of the query to look up; this
 * is used to lookup just about *any* (string) capability.
 * Returns TRU1 on success and TRUM1 for queries for which a builtin default
 * is returned; FAL0 is returned on non-availability */
FL bool_t      n_termcap_query(enum n_termcap_query query,
                  struct n_termcap_value *tvp);

/* Get a n_termcap_query for name or -1 if it is not known, and -2 if
 * type wasn't _NONE and the type doesn't match. */
# ifdef HAVE_KEY_BINDINGS
FL si32_t      n_termcap_query_for_name(char const *name,
                  enum n_termcap_captype type);
FL char const *n_termcap_name_of_query(enum n_termcap_query query);
# endif
#endif /* n_HAVE_TCAP */

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
 * tty.c
 */

/* Return wether user says yes, on STDIN if interactive.
 * Uses noninteract_default, the return value for non-interactive use cases,
 * as a hint for boolify() and chooses the yes/no string to append to prompt
 * accordingly.  If prompt is NULL "Continue" is used instead.
 * Handles+reraises SIGINT */
FL bool_t      getapproval(char const *prompt, bool_t noninteract_default);

#ifdef HAVE_SOCKETS
/* Get a password the expected way, return termios_state.ts_linebuf on
 * success or NULL on error */
FL char *      getuser(char const *query);

/* Get a password the expected way, return termios_state.ts_linebuf on
 * success or NULL on error.  SIGINT is temporarily blocked, *not* reraised.
 * termios_state_reset() (def.h) must be called anyway */
FL char *      getpassword(char const *query);
#endif

/* Overall interactive terminal life cycle for command line editor library */
#if defined HAVE_READLINE
# define TTY_WANTS_SIGWINCH
#endif
FL void        n_tty_init(void);
FL void        n_tty_destroy(void);

/* Rather for main.c / SIGWINCH interaction only */
FL void        n_tty_signal(int sig);

/* Read a line after printing prompt (if set and non-empty).
 * If n>0 assumes that *linebuf has n bytes of default content */
FL int         n_tty_readline(char const *prompt, char **linebuf,
                  size_t *linesize, size_t n SMALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define n_tty_readline(A,B,C,D) (n_tty_readline)(A, B, C, D, __FILE__,__LINE__)
#endif

/* Add a line (most likely as returned by n_tty_readline()) to the history.
 * Wether an entry added for real depends on the isgabby / *history-gabby*
 * relation, and / or wether s is non-empty and doesn't begin with U+0020 */
FL void        n_tty_addhist(char const *s, bool_t isgabby);

#ifdef HAVE_HISTORY
FL int         c_history(void *v);
#else
# define c_history               c_cmdnotsupp
#endif

/*
 * ui_str.c
 */

/* Detect visual width of (blen bytes of) buf, return (size_t)-1 on error.
 * Give blen UIZ_MAX to strlen().   buf may be NULL if (final) blen is 0 */
FL size_t      field_detect_width(char const *buf, size_t blen);

/* Check (multibyte-safe) how many bytes of buf (which is blen byts) can be
 * safely placed in a buffer (field width) of maxlen bytes */
FL size_t      field_detect_clip(size_t maxlen, char const *buf, size_t blen);

/* Put maximally maxlen bytes of buf, a buffer of blen bytes, into store,
 * taking into account multibyte code point boundaries and possibly
 * encapsulating in bidi_info toggles as necessary */
FL size_t      field_put_bidi_clip(char *store, size_t maxlen, char const *buf,
                  size_t blen);

/* Place cp in a salloc()ed buffer, column-aligned; for header display only */
FL char *      colalign(char const *cp, int col, int fill,
                  int *cols_decr_used_or_null);

/* Convert a string to a displayable one;
 * prstr() returns the result savestr()d, prout() writes it */
FL void        makeprint(struct str const *in, struct str *out);
FL size_t      delctrl(char *cp, size_t len);
FL char *      prstr(char const *s);
FL int         prout(char const *s, size_t sz, FILE *fp);

/* Print out a Unicode character or a substitute for it, return 0 on error or
 * wcwidth() (or 1) on success */
FL size_t      putuc(int u, int c, FILE *fp);

/* Check wether bidirectional info maybe needed for blen bytes of bdat */
FL bool_t      bidi_info_needed(char const *bdat, size_t blen);

/* Create bidirectional text encapsulation information; without HAVE_NATCH_CHAR
 * the strings are always empty */
FL void        bidi_info_create(struct bidi_info *bip);

/*
 * urlcrecry.c
 */

/* URL en- and decoding according to (enough of) RFC 3986 (RFC 1738).
 * These return a newly salloc()ated result */
FL char *      urlxenc(char const *cp, bool_t ispath SALLOC_DEBUG_ARGS);
FL char *      urlxdec(char const *cp SALLOC_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define urlxenc(CP,P)           urlxenc(CP, P, __FILE__, __LINE__)
# define urlxdec(CP)             urlxdec(CP, __FILE__, __LINE__)
#endif

#ifdef HAVE_SOCKETS
/* Return port of urlp->url_proto (and set irv_or_null), or NULL if unknown */
FL char const * url_servbyname(struct url const *urlp, ui16_t *irv_or_null);

/* Parse data, which must meet the criteria of the protocol cproto, and fill
 * in the URL structure urlp (URL rather according to RFC 3986) */
FL bool_t      url_parse(struct url *urlp, enum cproto cproto,
                  char const *data);

/* Zero ccp and lookup credentials for communicating with urlp.
 * Return wether credentials are available and valid (for chosen auth) */
FL bool_t      ccred_lookup(struct ccred *ccp, struct url *urlp);
FL bool_t      ccred_lookup_old(struct ccred *ccp, enum cproto cproto,
                  char const *addr);
#endif /* HAVE_SOCKETS */

/* `netrc' */
#ifdef HAVE_NETRC
FL int         c_netrc(void *v);
#else
# define c_netrc                 c_cmdnotsupp
#endif

/* MD5 (RFC 1321) related facilities */
#ifdef HAVE_MD5
# ifdef HAVE_OPENSSL_MD5
#  define md5_ctx	               MD5_CTX
#  define md5_init	            MD5_Init
#  define md5_update	            MD5_Update
#  define md5_final	            MD5_Final
# else
   /* The function definitions are instantiated in main.c */
#  include "rfc1321.h"
# endif

/* Store the MD5 checksum as a hexadecimal string in *hex*, *not* terminated,
 * using lowercase ASCII letters as defined in RFC 2195 */
# define MD5TOHEX_SIZE           32
FL char *      md5tohex(char hex[MD5TOHEX_SIZE], void const *vp);

/* CRAM-MD5 encode the *user* / *pass* / *b64* combo */
FL char *      cram_md5_string(struct str const *user, struct str const *pass,
                  char const *b64);

/* RFC 2104: HMAC: Keyed-Hashing for Message Authentication.
 * unsigned char *text: pointer to data stream
 * int text_len       : length of data stream
 * unsigned char *key : pointer to authentication key
 * int key_len        : length of authentication key
 * caddr_t digest     : caller digest to be filled in */
FL void        hmac_md5(unsigned char *text, int text_len, unsigned char *key,
                  int key_len, void *digest);
#endif /* HAVE_MD5 */

#ifndef HAVE_AMALGAMATION
# undef FL
# define FL
#endif

/* s-it-mode */
