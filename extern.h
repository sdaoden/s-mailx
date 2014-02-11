/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Exported function prototypes.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*-
 * Copyright (c) 1992, 1993
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

/*
 * acmava.c
 */

/* Don't use _var_* unless you *really* have to! */

/* Constant option key look/(un)set/clear */
FL char *   _var_oklook(enum okeys okey);
#define ok_blook(C)              (_var_oklook(CONCAT(ok_b_, C)) != NULL)
#define ok_vlook(C)              _var_oklook(CONCAT(ok_v_, C))

FL bool_t   _var_okset(enum okeys okey, uintptr_t val);
#define ok_bset(C,B)             _var_okset(CONCAT(ok_b_, C), (uintptr_t)(B))
#define ok_vset(C,V)             _var_okset(CONCAT(ok_v_, C), (uintptr_t)(V))

FL bool_t   _var_okclear(enum okeys okey);
#define ok_bclear(C)             _var_okclear(CONCAT(ok_b_, C))
#define ok_vclear(C)             _var_okclear(CONCAT(ok_v_, C))

/* Variable option key look/(un)set/clear */
FL char *   _var_voklook(char const *vokey);
#define vok_blook(S)              (_var_voklook(S) != NULL)
#define vok_vlook(S)              _var_voklook(S)

FL bool_t   _var_vokset(char const *vokey, uintptr_t val);
#define vok_bset(S,B)            _var_vokset(S, (uintptr_t)(B))
#define vok_vset(S,V)            _var_vokset(S, (uintptr_t)(V))

FL bool_t   _var_vokclear(char const *vokey);
#define vok_bclear(S)            _var_vokclear(S)
#define vok_vclear(S)            _var_vokclear(S)

/* List all variables */
FL void     var_list_all(void);

FL int      c_var_inspect(void *v);
FL int      c_define(void *v);
FL int      c_undef(void *v);
FL int      c_call(void *v);

FL int      callhook(char const *name, int nmail);

/* List all macros */
FL int      c_defines(void *v);

/* `account' */
FL int      c_account(void *v);

/* `localopts' */
FL int      c_localopts(void *v);

/*
 * attachments.c
 */

/* Try to add an attachment for *file*, file_expand()ed.
 * Return the new head of list *aphead*, or NULL.
 * The newly created attachment will be stored in **newap*, if given */
FL struct attachment *  add_attachment(struct attachment *aphead, char *file,
                           struct attachment **newap);

/* Append comma-separated list of file names to the end of attachment list */
FL struct attachment *  append_attachments(struct attachment *aphead,
                           char *names);

/* Interactively edit the attachment list, return the new list head */
FL struct attachment *  edit_attachments(struct attachment *aphead);

/*
 * auxlily.c
 */

/* Announce a fatal error (and die) */
FL void        panic(char const *format, ...);
FL void        alert(char const *format, ...);

/* Provide BSD-like signal() on all (POSIX) systems */
FL sighandler_type safe_signal(int signum, sighandler_type handler);

/* Hold *all* signals but SIGCHLD, and release that total block again */
FL void        hold_all_sigs(void);
FL void        rele_all_sigs(void);

/* Hold HUP/QUIT/INT */
FL void        hold_sigs(void);
FL void        rele_sigs(void);

/* Not-Yet-Dead debug information (handler installation in main.c) */
#ifdef HAVE_DEBUG
FL void        _nyd_chirp(ui8_t act, char const *file, ui32_t line,
                  char const *fun);
FL void        _nyd_oncrash(int signo);

# define NYD_ENTER               _nyd_chirp(1, __FILE__, __LINE__, __FUN__)
# define NYD_LEAVE               _nyd_chirp(2, __FILE__, __LINE__, __FUN__)
# define NYD                     _nyd_chirp(0, __FILE__, __LINE__, __FUN__)
# define NYD_X                   _nyd_chirp(0, __FILE__, __LINE__, __FUN__)
#else
# define NYD_ENTER               do {} while (0)
# define NYD_LEAVE               do {} while (0)
# define NYD                     do {} while (0)
# define NYD_X                   do {} while (0) /* XXX LEGACY */
#endif

/* Touch the named message by setting its MTOUCH flag.  Touched messages have
 * the effect of not being sent back to the system mailbox on exit */
FL void        touch(struct message *mp);

/* Test to see if the passed file name is a directory, return true if it is */
FL bool_t      is_dir(char const *name);

/* Count the number of arguments in the given string raw list */
FL int         argcount(char **argv);

/* Compute screen size */
FL int         screensize(void);

/* Get our PAGER */
FL char const *get_pager(void);

/* Check wether using a pager is possible/makes sense and is desired by user
 * (*crt* set); return number of screen lines (or *crt*) if so, 0 otherwise */
FL size_t      paging_seems_sensible(void);

/* Use a pager or STDOUT to print *fp*; if *lines* is 0, they'll be counted */
FL void        page_or_print(FILE *fp, size_t lines);
#define try_pager(FP)            page_or_print(FP, 0) /* TODO obsolete */

/* Parse name and guess at the required protocol */
FL enum protocol  which_protocol(char const *name);

/* Hash the passed string -- uses Chris Torek's hash algorithm */
FL ui32_t      torek_hash(char const *name);
#define hash(S)                  (torek_hash(S) % HSHSIZE) /* xxx COMPAT (?) */

/* Create hash */
FL ui32_t      pjw(char const *cp); /* TODO obsolete -> torek_hash() */

/* Find a prime greater than n */
FL ui32_t      nextprime(ui32_t n);

/* Check wether *s is an escape sequence, expand it as necessary.
 * Returns the expanded sequence or 0 if **s is NUL or PROMPT_STOP if it is \c.
 * *s is advanced to after the expanded sequence (as possible).
 * If use_prompt_extensions is set, an enum prompt_exp may be returned */
FL int         expand_shell_escape(char const **s,
                  bool_t use_prompt_extensions);

/* Get *prompt*, or '& ' if *bsdcompat*, of '? ' otherwise */
FL char *      getprompt(void);

/* Detect and query the hostname to use */
FL char *      nodename(int mayoverride);

/* Try to lookup a variable named "password-*token*".
 * Return NULL or salloc()ed buffer */
FL char *      lookup_password_for_token(char const *token);

/* Get a (pseudo) random string of *length* bytes; returns salloc()ed buffer */
FL char *      getrandstring(size_t length);

#define Hexchar(n)               ((n)>9 ? (n)-10+'A' : (n)+'0')
#define hexchar(n)               ((n)>9 ? (n)-10+'a' : (n)+'0')

/* MD5 (RFC 1321) related facilities */
#ifdef HAVE_MD5
# ifdef HAVE_OPENSSL_MD5
#  define md5_ctx	               MD5_CTX
#  define md5_init	            MD5_Init
#  define md5_update	            MD5_Update
#  define md5_final	            MD5_Final
# else
#  include "rfc1321.h"
# endif

/* Store the MD5 checksum as a hexadecimal string in *hex*, *not* terminated */
# define MD5TOHEX_SIZE           32
FL char *      md5tohex(char hex[MD5TOHEX_SIZE], void const *vp);

/* CRAM-MD5 encode the *user* / *pass* / *b64* combo */
FL char *      cram_md5_string(char const *user, char const *pass,
                  char const *b64);

/* RFC 2104: HMAC: Keyed-Hashing for Message Authentication.
 * unsigned char *text: pointer to data stream
 * int text_len       : length of data stream
 * unsigned char *key : pointer to authentication key
 * int key_len        : length of authentication key
 * caddr_t digest     : caller digest to be filled in */
FL void        hmac_md5(unsigned char *text, int text_len, unsigned char *key,
                  int key_len, void *digest);
#endif

FL enum okay   makedir(char const *name);

/* A get-wd..restore-wd approach */
FL enum okay   cwget(struct cw *cw);
FL enum okay   cwret(struct cw *cw);
FL void        cwrelse(struct cw *cw);

/* xxx Place cp in a salloc()ed buffer, column-aligned */
FL char *      colalign(char const *cp, int col, int fill,
                  int *cols_decr_used_or_null);

/* Convert a string to a displayable one;
 * prstr() returns the result savestr()d, prout() writes it */
FL void        makeprint(struct str const *in, struct str *out);
FL char *      prstr(char const *s);
FL int         prout(char const *s, size_t sz, FILE *fp);

/* Print out a Unicode character or a substitute for it, return 0 on error or
 * wcwidth() (or 1) on success */
FL size_t      putuc(int u, int c, FILE *fp);

/* We want coloured output (in this salloc() cycle).  If pager_used is not NULL
 * we check against *colour-pagers* wether colour is really desirable */
#ifdef HAVE_COLOUR
FL void        colour_table_create(char const *pager_used);
FL void        colour_put(FILE *fp, enum colourspec cs);
FL void        colour_put_header(FILE *fp, char const *name);
FL void        colour_reset(FILE *fp);
FL struct str const * colour_get(enum colourspec cs);
#else
# define colour_put(FP,CS)
# define colour_put_header(FP,N)
# define colour_reset(FP)
#endif

/* Update *tc* to now; only .tc_time updated unless *full_update* is true */
FL void        time_current_update(struct time_current *tc,
                  bool_t full_update);

/* Memory allocation routines */
#ifdef HAVE_DEBUG
# define SMALLOC_DEBUG_ARGS      , char const *mdbg_file, int mdbg_line
# define SMALLOC_DEBUG_ARGSCALL  , mdbg_file, mdbg_line
#else
# define SMALLOC_DEBUG_ARGS
# define SMALLOC_DEBUG_ARGSCALL
#endif

FL void *      smalloc(size_t s SMALLOC_DEBUG_ARGS);
FL void *      srealloc(void *v, size_t s SMALLOC_DEBUG_ARGS);
FL void *      scalloc(size_t nmemb, size_t size SMALLOC_DEBUG_ARGS);

#ifdef HAVE_DEBUG
FL void        sfree(void *v SMALLOC_DEBUG_ARGS);
/* Called by sreset(), then */
FL void        smemreset(void);

FL int         c_smemtrace(void *v);
# if 0
#  define MEMCHECK
FL bool_t      _smemcheck(char const *file, int line);
# endif

# define smalloc(SZ)             smalloc(SZ, __FILE__, __LINE__)
# define srealloc(P,SZ)          srealloc(P, SZ, __FILE__, __LINE__)
# define scalloc(N,SZ)           scalloc(N, SZ, __FILE__, __LINE__)
# define free(P)                 sfree(P, __FILE__, __LINE__)
# define smemcheck()             _smemcheck(__FILE__, __LINE__)
#endif

/*
 * cmd1.c
 */

FL int         c_cmdnotsupp(void *v);

/* Show header group */
FL int         c_headers(void *v);

/* Scroll to the next/previous screen */
FL int         c_scroll(void *v);
FL int         c_Scroll(void *v);

/* Print out the headlines for each message in the passed message list */
FL int         c_from(void *v);

/* Print all message in between bottom and topx (including bottom) */
FL void        print_headers(size_t bottom, size_t topx);

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

FL int         c_respond(void *v);
FL int         c_respondall(void *v);
FL int         c_respondsender(void *v);
FL int         c_Respond(void *v);
FL int         c_followup(void *v);
FL int         c_followupall(void *v);
FL int         c_followupsender(void *v);
FL int         c_Followup(void *v);

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

/* Quit quickly.  If sourcing, just pop the input level by returning error */
FL int         c_rexit(void *v);

/* Set or display a variable value.  Syntax is similar to that of sh */
FL int         c_set(void *v);

/* Unset a bunch of variable values */
FL int         c_unset(void *v);

/* Put add users to a group */
FL int         c_group(void *v);

/* Delete the passed groups */
FL int         c_ungroup(void *v);

/* Change to another file.  With no argument, print info about current file */
FL int         c_file(void *v);

/* Expand file names like echo */
FL int         c_echo(void *v);

/* if.else.endif conditional execution.
 * condstack_isskip() returns wether the current condition state doesn't allow
 * execution of commands.
 * condstack_release() and condstack_take() are used when sourcing files, they
 * rotate the current condition stack; condstack_take() returns a false boolean
 * if the current condition stack has unclosed conditionals */
FL int         c_if(void *v);
FL int         c_else(void *v);
FL int         c_endif(void *v);
FL bool_t      condstack_isskip(void);
FL void *      condstack_release(void);
FL bool_t      condstack_take(void *self);

/* Set the list of alternate names */
FL int         c_alternates(void *v);

/* 'newmail' command: Check for new mail without writing old mail back */
FL int         c_newmail(void *v);

/* Shortcuts */
FL int         c_shortcut(void *v);
FL struct shortcut *get_shortcut(char const *str);
FL int         c_unshortcut(void *v);

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

/*
 * collect.c
 */

FL FILE *      collect(struct header *hp, int printheaders, struct message *mp,
                  char *quotefile, int doprefix);

FL void        savedeadletter(FILE *fp, int fflush_rewind_first);

/*
 * dotlock.c
 */

FL int         fcntl_lock(int fd, enum flock_type ft);
FL int         dot_lock(char const *fname, int fd, int pollinterval, FILE *fp,
                  char const *msg);
FL void        dot_unlock(char const *fname);

/*
 * edit.c
 */

/* Edit a message list */
FL int         c_editor(void *v);

/* Invoke the visual editor on a message list */
FL int         c_visual(void *v);

/* Run an editor on the file at fp of size bytes, and return a new file.
 * Signals must be handled by the caller.  viored is 'e' for ed, 'v' for vi */
FL FILE *      run_editor(FILE *fp, off_t size, int viored, int readonly,
                  struct header *hp, struct message *mp,
                  enum sendaction action, sighandler_type oldint);

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
#ifdef HAVE_DEBUG
# define fgetline(A,B,C,D,E,F)   \
   fgetline(A, B, C, D, E, F, __FILE__, __LINE__)
#endif

/* Read up a line from the specified input into the linebuffer.
 * Return the number of characters read.  Do not include the newline at EOL.
 * n is the number of characters already read */
FL int         readline_restart(FILE *ibuf, char **linebuf, size_t *linesize,
                  size_t n SMALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define readline_restart(A,B,C,D) \
   readline_restart(A, B, C, D, __FILE__, __LINE__)
#endif

/* Read a complete line of input, with editing if interactive and possible.
 * If prompt is NULL we'll call getprompt() first, if necessary.
 * nl_escape defines wether user can escape newlines via backslash (POSIX).
 * If string is set it is used as the initial line content if in interactive
 * mode, otherwise this argument is ignored for reproducibility.
 * Return number of octets or a value <0 on error */
FL int         readline_input(char const *prompt, bool_t nl_escape,
                  char **linebuf, size_t *linesize, char const *string
                  SMALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define readline_input(A,B,C,D,E) readline_input(A,B,C,D,E,__FILE__,__LINE__)
#endif

/* Read a line of input, with editing if interactive and possible, return it
 * savestr()d or NULL in case of errors or if an empty line would be returned.
 * This may only be called from toplevel (not during sourcing).
 * If prompt is NULL we'll call getprompt() if necessary.
 * If string is set it is used as the initial line content if in interactive
 * mode, otherwise this argument is ignored for reproducibility */
FL char *      readstr_input(char const *prompt, char const *string);

/* Set up the input pointers while copying the mail file into /tmp */
FL void        setptr(FILE *ibuf, off_t offset);

/* Drop the passed line onto the passed output buffer.  If a write error occurs
 * return -1, else the count of characters written, including the newline */
FL int         putline(FILE *obuf, char *linebuf, size_t count);

/* Return a file buffer all ready to read up the passed message pointer */
FL FILE *      setinput(struct mailbox *mp, struct message *m,
                  enum needspec need);

/* Reset (free) the global message array */
FL void        message_reset(void);

/* Append the passed message descriptor onto the message array; if mp is NULL,
 * NULLify the entry at &[msgCount-1] */
FL void        message_append(struct message *mp);

FL struct message * setdot(struct message *mp);

/* Delete a file, but only if the file is a plain file */
FL int         rm(char const *name);

/* Determine the size of the file possessed by the passed buffer */
FL off_t       fsize(FILE *iob);

/* Evaluate the string given as a new mailbox name. Supported meta characters:
 * %  for my system mail box
 * %user for user's system mail box
 * #  for previous file
 * &  invoker's mbox file
 * +file file in folder directory
 * any shell meta character
 * Returns the file name as an auto-reclaimed string */
FL char *      fexpand(char const *name, enum fexp_mode fexpm);

#define expand(N)                fexpand(N, FEXP_FULL)   /* XXX obsolete */
#define file_expand(N)           fexpand(N, FEXP_LOCAL)  /* XXX obsolete */

/* Get rid of queued mail */
FL void        demail(void);

/* acmava.c hook: *folder* variable has been updated; if folder shouldn't be
 * replaced by something else leave store alone, otherwise smalloc() the
 * desired value (ownership will be taken) */
FL bool_t      var_folder_updated(char const *folder, char **store);

/* Determine the current *folder* name, store it in *name* */
FL bool_t      getfold(char *name, size_t size);

/* Return the name of the dead.letter file */
FL char const * getdeadletter(void);

FL enum okay   get_body(struct message *mp);

/* Socket I/O */
#ifdef HAVE_SOCKETS
FL int         sclose(struct sock *sp);
FL enum okay   swrite(struct sock *sp, char const *data);
FL enum okay   swrite1(struct sock *sp, char const *data, int sz,
                  int use_buffer);
FL enum okay   sopen(char const *xserver, struct sock *sp, int use_ssl,
                  char const *uhp, char const *portstr);

/*  */
FL int         sgetline(char **line, size_t *linesize, size_t *linelen,
                  struct sock *sp SMALLOC_DEBUG_ARGS);
# ifdef HAVE_DEBUG
#  define sgetline(A,B,C,D)      sgetline(A, B, C, D, __FILE__, __LINE__)
# endif
#endif /* HAVE_SOCKETS */

/* Deal with loading of resource files and dealing with a stack of files for
 * the source command */

/* Load a file of user definitions */
FL void        load(char const *name);

/* Pushdown current input file and switch to a new one.  Set the global flag
 * *sourcing* so that others will realize that they are no longer reading from
 * a tty (in all probability) */
FL int         c_source(void *v);

/* Pop the current input back to the previous level.  Update the *sourcing*
 * flag as appropriate */
FL int         unstack(void);

/*
 * head.c
 */

/* Return the user's From: address(es) */
FL char const * myaddrs(struct header *hp);

/* Boil the user's From: addresses down to a single one, or use *sender* */
FL char const * myorigin(struct header *hp);

/* See if the passed line buffer, which may include trailing newline (sequence)
 * is a mail From_ header line according to RFC 4155 */
FL int         is_head(char const *linebuf, size_t linelen);

/* Savage extract date field from From_ line.  linelen is convenience as line
 * must be terminated (but it may end in a newline [sequence]).
 * Return wether the From_ line was parsed successfully */
FL int         extract_date_from_from_(char const *line, size_t linelen,
                  char datebuf[FROM_DATEBUF]);

/* Fill in / reedit the desired header fields */
FL int         grab_headers(struct header *hp, enum gfield gflags,
                  int subjfirst);

FL void        extract_header(FILE *fp, struct header *hp);

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

/* Check if a name's address part contains invalid characters */
FL int         is_addr_invalid(struct name *np, int putmsg);

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

FL int         msgidcmp(char const *s1, char const *s2);

/* See if the given header field is supposed to be ignored */
FL int         is_ign(char const *field, size_t fieldlen,
                  struct ignoretab ignore[2]);

FL int         member(char const *realfield, struct ignoretab *table);

/* Fake Sender for From_ lines if missing, e. g. with POP3 */
FL char const * fakefrom(struct message *mp);

FL char const * fakedate(time_t t);

/* From username Fri Jan  2 20:13:51 2004
 *               |    |    |    |    |
 *               0    5   10   15   20 */
FL time_t      unixtime(char const *from);

FL time_t      rfctime(char const *date);

FL time_t      combinetime(int year, int month, int day,
                  int hour, int minute, int second);

FL void        substdate(struct message *m);

FL int         check_from_and_sender(struct name *fromfield,
                  struct name *senderfield);

FL char *      getsender(struct message *m);

/*
 * imap.c
 */

#ifdef HAVE_IMAP
FL char const * imap_fileof(char const *xcp);
FL enum okay   imap_noop(void);
FL enum okay   imap_select(struct mailbox *mp, off_t *size, int *count,
                  const char *mbx);
FL int         imap_setfile(const char *xserver, int nmail, int isedit);
FL enum okay   imap_header(struct message *m);
FL enum okay   imap_body(struct message *m);
FL void        imap_getheaders(int bot, int top);
FL void        imap_quit(void);
FL enum okay   imap_undelete(struct message *m, int n);
FL enum okay   imap_unread(struct message *m, int n);
FL int         c_imap_imap(void *vp);
FL int         imap_newmail(int nmail);
FL enum okay   imap_append(const char *xserver, FILE *fp);
FL void        imap_folders(const char *name, int strip);
FL enum okay   imap_copy(struct message *m, int n, const char *name);
# ifdef HAVE_IMAP_SEARCH
FL enum okay   imap_search1(const char *spec, int f);
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
#else
# define c_imap_imap             c_cmdnotsupp
# define c_connect               c_cmdnotsupp
# define c_disconnect            c_cmdnotsupp
# define c_cache                 c_cmdnotsupp
#endif

#if defined HAVE_IMAP || defined HAVE_IMAP_SEARCH
FL char *      imap_quotestr(char const *s);
FL char *      imap_unquotestr(char const *s);
#endif

/*
 * imap_cache.c
 */

#ifdef HAVE_IMAP
FL enum okay   getcache1(struct mailbox *mp, struct message *m,
                  enum needspec need, int setflags);
FL enum okay   getcache(struct mailbox *mp, struct message *m,
                  enum needspec need);
FL void        putcache(struct mailbox *mp, struct message *m);
FL void        initcache(struct mailbox *mp);
FL void        purgecache(struct mailbox *mp, struct message *m, long mc);
FL void        delcache(struct mailbox *mp, struct message *m);
FL enum okay   cache_setptr(int transparent);
FL enum okay   cache_list(struct mailbox *mp, char const *base, int strip,
                  FILE *fp);
FL enum okay   cache_remove(char const *name);
FL enum okay   cache_rename(char const *old, char const *new);
FL unsigned long cached_uidvalidity(struct mailbox *mp);
FL FILE *      cache_queue(struct mailbox *mp);
FL enum okay   cache_dequeue(struct mailbox *mp);
#endif /* HAVE_IMAP */

/*
 * imap_search.c
 */

#ifdef HAVE_IMAP_SEARCH
FL enum okay   imap_search(char const *spec, int f);
#endif

/*
 * lex.c
 */

/* Set up editing on the given file name.
 * If the first character of name is %, we are considered to be editing the
 * file, otherwise we are reading our mail which has signficance for mbox and
 * so forth.  nmail: Check for new mail in the current folder only */
FL int         setfile(char const *name, int nmail);

FL int         newmailinfo(int omsgCount);

/* Interpret user commands.  If standard input is not a tty, print no prompt */
FL void        commands(void);

/* Evaluate a single command.
 * .ev_add_history and .ev_new_content will be updated upon success.
 * Command functions return 0 for success, 1 for error, and -1 for abort.
 * 1 or -1 aborts a load or source, a -1 aborts the interactive command loop */
FL int         evaluate(struct eval_ctx *evp);
/* TODO drop execute() is the legacy version of evaluate().
 * Contxt is non-zero if called while composing mail */
FL int         execute(char *linebuf, int contxt, size_t linesize);

/* Set the size of the message vector used to construct argument lists to
 * message list functions */
FL void        setmsize(int sz);

/* The following gets called on receipt of an interrupt.  This is to abort
 * printout of a command, mainly.  Dispatching here when command() is inactive
 * crashes rcv.  Close all open files except 0, 1, 2, and the temporary.  Also,
 * unstack all source files */
FL void        onintr(int s);

/* Announce the presence of the current Mail version, give the message count,
 * and print a header listing */
FL void        announce(int printheaders);

/* Announce information about the file we are editing.  Return a likely place
 * to set dot */
FL int         newfileinfo(void);

FL int         getmdot(int nmail);

FL void        initbox(char const *name);

/* Print the docstring of `comm', which may be an abbreviation.
 * Return FAL0 if there is no such command */
#ifdef HAVE_DOCSTRINGS
FL bool_t      print_comm_docstr(char const *comm);
#endif

/*
 * list.c
 */

/* Convert user string of message numbers and store the numbers into vector.
 * Returns the count of messages picked up or -1 on error */
FL int         getmsglist(char *buf, int *vector, int flags);

/* Scan out the list of string arguments, shell style for a RAWLIST */
FL int         getrawlist(char const *line, size_t linesize,
                  char **argv, int argc, int echolist);

/* Find the first message whose flags&m==f and return its message number */
FL int         first(int f, int m);

/* Mark the named message by setting its mark bit */
FL void        mark(int mesg, int f);

/* lzw.c TODO drop */
#ifdef HAVE_IMAP
FL int         zwrite(void *cookie, const char *wbp, int num);
FL int         zfree(void *cookie);
FL int         zread(void *cookie, char *rbp, int num);
FL void *      zalloc(FILE *fp);
#endif /* HAVE_IMAP */

/*
 * maildir.c
 */

FL int         maildir_setfile(char const *name, int nmail, int isedit);

FL void        maildir_quit(void);

FL enum okay   maildir_append(char const *name, FILE *fp);

FL enum okay   maildir_remove(char const *name);

/*
 * mime.c
 */

/* *charset-7bit*, else CHARSET_7BIT */
FL char const * charset_get_7bit(void);

/* *charset-8bit*, else CHARSET_8BIT */
FL char const * charset_get_8bit(void);

/* LC_CTYPE:CODESET / *ttycharset*, else *charset-8bit*, else CHARSET_8BIT */
FL char const * charset_get_lc(void);

/* *sendcharsets* / *charset-8bit* iterator.
 * *a_charset_to_try_first* may be used to prepend a charset (as for
 * *reply-in-same-charset*);  works correct for !HAVE_ICONV */
FL void        charset_iter_reset(char const *a_charset_to_try_first);
FL char const * charset_iter_next(void);
FL char const * charset_iter_current(void);
FL void        charset_iter_recurse(char *outer_storage[2]); /* TODO LEGACY */
FL void        charset_iter_restore(char *outer_storage[2]); /* TODO LEGACY */

FL char const * need_hdrconv(struct header *hp, enum gfield w);

/* Get the mime encoding from a Content-Transfer-Encoding header field */
FL enum mimeenc mime_getenc(char *h);

/* Get a mime style parameter from a header line */
FL char *      mime_getparam(char const *param, char *h);

/* Get the boundary out of a Content-Type: multipart/xyz header field, return
 * salloc()ed copy of it; store strlen() in *len if set */
FL char *      mime_get_boundary(char *h, size_t *len);

/* Create a salloc()ed MIME boundary */
FL char *      mime_create_boundary(void);

/* Classify content of *fp* as necessary and fill in arguments; **charset* is
 * left alone unless it's non-NULL */
FL int         mime_classify_file(FILE *fp, char const **contenttype,
                  char const **charset, int *do_iconv);

/* */
FL enum mimecontent mime_classify_content_of_part(struct mimepart const *mip);

/* Return the Content-Type matching the extension of name */
FL char *      mime_classify_content_type_by_fileext(char const *name);

/* "mimetypes" command */
FL int         c_mimetypes(void *v);

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
                  FILE *f, enum conversion convert, enum tdflags dflags,
                  struct str *rest);

/*
 * mime_cte.c
 * Content-Transfer-Encodings as defined in RFC 2045:
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8
 */

/* How many characters of (the complete body) ln need to be quoted */
FL size_t      mime_cte_mustquote(char const *ln, size_t lnlen, bool_t ishead);

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
FL struct str * b64_encode_cp(struct str *out, char const *cp,
                  enum b64flags flags);
FL struct str * b64_encode_buf(struct str *out, void const *vp, size_t vp_len,
                  enum b64flags flags);

/* If rest is set then decoding will assume text input.
 * The buffers of out and possibly rest will be managed via srealloc().
 * Returns OKAY or STOP on error (in which case out is set to an error
 * message); caller is responsible to free buffers */
FL int         b64_decode(struct str *out, struct str const *in,
                  struct str *rest);

/*
 * names.c
 */

/* Allocate a single element of a name list, initialize its name field to the
 * passed name and return it */
FL struct name * nalloc(char *str, enum gfield ntype);

/* Like nalloc(), but initialize from content of np */
FL struct name * ndup(struct name *np, enum gfield ntype);

/* Concatenate the two passed name lists, return the result */
FL struct name * cat(struct name *n1, struct name *n2);

/* Determine the number of undeleted elements in a name list and return it */
FL ui32_t      count(struct name const *np);

/* Extract a list of names from a line, and make a list of names from it.
 * Return the list or NULL if none found */
FL struct name * extract(char const *line, enum gfield ntype);

/* Like extract() unless line contains anyof ",\"\\(<|", in which case
 * comma-separated list extraction is used instead */
FL struct name * lextract(char const *line, enum gfield ntype);

/* Turn a list of names into a string of the same names */
FL char *      detract(struct name *np, enum gfield ntype);

/* Get a lextract() list via readstr_input(), reassigning to *np* */
FL struct name * grab_names(char const *field, struct name *np, int comma,
                     enum gfield gflags);

/* Check all addresses in np and delete invalid ones */
FL struct name * checkaddrs(struct name *np);

/* Map all of the aliased users in the invoker's mailrc file and insert them
 * into the list */
FL struct name * usermap(struct name *names, bool_t force_metoo);

/* Remove all of the duplicates from the passed name list by insertion sorting
 * them, then checking for dups.  Return the head of the new list */
FL struct name * elide(struct name *names);

FL struct name * delete_alternates(struct name *np);

FL int         is_myname(char const *name);

/* Dispatch a message to all pipe and file addresses TODO -> sendout.c */
FL struct name * outof(struct name *names, FILE *fo, struct header *hp,
                  bool_t *senderror);

/* Handling of alias groups */

/* Locate a group name and return it */
FL struct grouphead * findgroup(char *name);

/* Print a group out on stdout */
FL void        printgroup(char *name);

FL void        remove_group(char const *name);

/*
 * openssl.c
 */

#ifdef HAVE_OPENSSL
/*  */
FL enum okay   ssl_open(char const *server, struct sock *sp, char const *uhp);

/*  */
FL void        ssl_gen_err(char const *fmt, ...);

/*  */
FL int         c_verify(void *vp);

/*  */
FL FILE *      smime_sign(FILE *ip, struct header *);

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
 * pop3.c
 */

#ifdef HAVE_POP3
/*  */
FL enum okay   pop3_noop(void);

/*  */
FL int         pop3_setfile(char const *server, int nmail, int isedit);

/*  */
FL enum okay   pop3_header(struct message *m);

/*  */
FL enum okay   pop3_body(struct message *m);

/*  */
FL void        pop3_quit(void);
#endif /* HAVE_POP3 */

/*
 * popen.c
 * Subprocesses, popen, but also file handling with registering
 */

/* Notes: OF_CLOEXEC is implied in oflags, xflags may be NULL */
FL FILE *      safe_fopen(char const *file, char const *oflags, int *xflags);

/* Notes: OF_CLOEXEC|OF_REGISTER are implied in oflags */
FL FILE *      Fopen(char const *file, char const *oflags);

FL FILE *      Fdopen(int fd, char const *oflags);

FL int         Fclose(FILE *fp);

FL FILE *      Zopen(char const *file, char const *oflags, int *compression);

/* Create a temporary file in tempdir, use prefix for its name, store the
 * unique name in fn (unless OF_UNLINK is set in oflags), and return a stdio
 * FILE pointer with access oflags.  OF_CLOEXEC is implied in oflags.
 * mode specifies the access mode of the newly created temporary file */
FL FILE *      Ftmp(char **fn, char const *prefix, enum oflags oflags,
                  int mode);

/* If OF_HOLDSIGS was set when calling Ftmp(), then hold_all_sigs() had been
 * called: call this to unlink(2) and free *fn and to rele_all_sigs() */
FL void        Ftmp_release(char **fn);

/* Free the resources associated with the given filename.  To be called after
 * unlink() */
FL void        Ftmp_free(char **fn);

/* Create a pipe and ensure CLOEXEC bit is set in both descriptors */
FL bool_t      pipe_cloexec(int fd[2]);

FL FILE *      Popen(char const *cmd, char const *mode, char const *shell,
                  int newfd1);

FL bool_t      Pclose(FILE *ptr, bool_t dowait);

FL void        close_all_files(void);

/* Run a command without a shell, with optional arguments and splicing of stdin
 * and stdout.  The command name can be a sequence of words.  Signals must be
 * handled by the caller.  "Mask" contains the signals to ignore in the new
 * process.  SIGINT is enabled unless it's in the mask */
FL int         run_command(char const *cmd, sigset_t *mask, int infd,
                  int outfd, char const *a0, char const *a1, char const *a2);

FL int         start_command(char const *cmd, sigset_t *mask, int infd,
                  int outfd, char const *a0, char const *a1, char const *a2);

FL void        prepare_child(sigset_t *nset, int infd, int outfd);

FL void        sigchild(int signo);

/* Mark a child as don't care */
FL void        free_child(int pid);

/* Wait for pid, return wether we've had a normal EXIT_SUCCESS exit.
 * If wait_status is set, set it to the reported waitpid(2) wait status */
FL bool_t      wait_child(int pid, int *wait_status);

/*
 * quit.c
 */

/* The `quit' command */
FL int         c_quit(void *v);

/* Save all of the undetermined messages at the top of "mbox".  Save all
 * untouched messages back in the system mailbox.  Remove the system mailbox,
 * if none saved there */
FL void        quit(void);

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
                  char const *prefix, enum sendaction action, off_t *stats);

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

/* Dump the to, subject, cc header on the passed file buffer */
FL int         puthead(struct header *hp, FILE *fo, enum gfield w,
                  enum sendaction action, enum conversion convert,
                  char const *contenttype, char const *charset);

/*  */
FL enum okay   resend_msg(struct message *mp, struct name *to, int add_resent);

/*
 * smtp.c
 */

#ifdef HAVE_SMTP
/* smtp-authXY, where XY=type=-user|-password etc */
FL char *      smtp_auth_var(char const *type, char const *addr);

/* Connect to a SMTP server */
FL int         smtp_mta(char *server, struct name *to, FILE *fi,
                  struct header *hp, char const *user, char const *password,
                  char const *skinned);
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
FL void        ssl_set_verify_level(char const *uhp);

/*  */
FL enum okay   ssl_verify_decide(void);

/*  */
FL char *      ssl_method_string(char const *uhp);

/*  */
FL enum okay   smime_split(FILE *ip, FILE **hp, FILE **bp, long xcount,
                  int keep);

/*  */
FL FILE *      smime_sign_assemble(FILE *hp, FILE *bp, FILE *sp);

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
 * This bundles several different string related support facilities:
 * - auto-reclaimed string storage (memory goes away on command loop ticks)
 * - plain char* support functions which use unspecified or smalloc() memory
 * - struct str related support funs
 * - our iconv(3) wrapper
 */

/* Auto-reclaimed string storage */

#ifdef HAVE_DEBUG
# define SALLOC_DEBUG_ARGS       , char const *mdbg_file, int mdbg_line
# define SALLOC_DEBUG_ARGSCALL   , mdbg_file, mdbg_line
#else
# define SALLOC_DEBUG_ARGS
# define SALLOC_DEBUG_ARGSCALL
#endif

/* Allocate size more bytes of space and return the address of the first byte
 * to the caller.  An even number of bytes are always allocated so that the
 * space will always be on a word boundary */
FL void *      salloc(size_t size SALLOC_DEBUG_ARGS);
FL void *      csalloc(size_t nmemb, size_t size SALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
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
#ifdef HAVE_DEBUG
FL int         c_sstats(void *v);
#endif

/* Return a pointer to a dynamic copy of the argument */
FL char *      savestr(char const *str SALLOC_DEBUG_ARGS);
FL char *      savestrbuf(char const *sbuf, size_t sbuf_len SALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define savestr(CP)             savestr(CP, __FILE__, __LINE__)
# define savestrbuf(CBP,CBL)     savestrbuf(CBP, CBL, __FILE__, __LINE__)
#endif

/* Make copy of argument incorporating old one, if set, separated by space */
FL char *      save2str(char const *str, char const *old SALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define save2str(S,O)           save2str(S, O, __FILE__, __LINE__)
#endif

/* strcat */
FL char *      savecat(char const *s1, char const *s2 SALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define savecat(S1,S2)          savecat(S1, S2, __FILE__, __LINE__)
#endif

/* Create duplicate, lowercasing all characters along the way */
FL char *      i_strdup(char const *src SALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define i_strdup(CP)            i_strdup(CP, __FILE__, __LINE__)
#endif

/* Extract the protocol base and return a duplicate */
FL char *      protbase(char const *cp SALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define protbase(CP)            protbase(CP, __FILE__, __LINE__)
#endif

/* URL en- and decoding (RFC 1738, but not really) */
FL char *      urlxenc(char const *cp SALLOC_DEBUG_ARGS);
FL char *      urlxdec(char const *cp SALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define urlxenc(CP)             urlxenc(CP, __FILE__, __LINE__)
# define urlxdec(CP)             urlxdec(CP, __FILE__, __LINE__)
#endif

/*  */
FL struct str * str_concat_csvl(struct str *self, ...);
FL struct str * str_concat_cpa(struct str *self, char const * const *cpa,
                  char const *sep_o_null SALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define str_concat_cpa(S,A,N)   str_concat_cpa(S, A, N, __FILE__, __LINE__)
#endif

/* Plain char* support, not auto-reclaimed (unless noted) */

/* Are any of the characters in the two strings the same? */
FL int         anyof(char const *s1, char const *s2);

/* Treat *iolist as a sep separated list of strings; find and return the
 * next entry, trimming surrounding whitespace, and point *iolist to the next
 * entry or to NULL if no more entries are contained.  If ignore_empty is not
 * set empty entries are started over.  Return NULL or an entry */
FL char *      n_strsep(char **iolist, char sep, bool_t ignore_empty);

/* Copy a string, lowercasing it as we go; *size* is buffer size of *dest*;
 * *dest* will always be terminated unless *size* is 0 */
FL void        i_strcpy(char *dest, char const *src, size_t size);

/* Is *as1* a valid prefix of *as2*? */
FL int         is_prefix(char const *as1, char const *as2);

/* Find the last AT @ before the first slash */
FL char const * last_at_before_slash(char const *sp);

/* Get (and isolate) the last, possibly quoted part of linebuf, set *needs_list
 * to indicate wether getmsglist() et al need to be called to collect
 * additional args that remain in linebuf.  Return NULL on "error" */
FL char *      laststring(char *linebuf, bool_t *needs_list, bool_t strip);

/* Convert a string to lowercase, in-place and with multibyte-aware */
FL void        makelow(char *cp);

/* Is *sub* a substring of *str*, case-insensitive and multibyte-aware? */
FL bool_t      substr(char const *str, char const *sub);

/* Lazy vsprintf wrapper */
#ifndef HAVE_SNPRINTF
FL int         snprintf(char *str, size_t size, char const *format, ...);
#endif

FL char *      sstpcpy(char *dst, char const *src);
FL char *      sstrdup(char const *cp SMALLOC_DEBUG_ARGS);
FL char *      sbufdup(char const *cp, size_t len SMALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define sstrdup(CP)             sstrdup(CP, __FILE__, __LINE__)
# define sbufdup(CP,L)           sbufdup(CP, L, __FILE__, __LINE__)
#endif

FL char *      n_strlcpy(char *dst, char const *src, size_t len);

/* Locale-independent character class functions */
FL int         asccasecmp(char const *s1, char const *s2);
FL int         ascncasecmp(char const *s1, char const *s2, size_t sz);
FL char const * asccasestr(char const *haystack, char const *xneedle);
FL bool_t      is_asccaseprefix(char const *as1, char const *as2);

/* struct str related support funs */

/* *self->s* is srealloc()ed */
FL struct str * n_str_dup(struct str *self, struct str const *t
                  SMALLOC_DEBUG_ARGS);

/* *self->s* is srealloc()ed, *self->l* incremented */
FL struct str * n_str_add_buf(struct str *self, char const *buf, size_t buflen
                  SMALLOC_DEBUG_ARGS);
#define n_str_add(S, T)          n_str_add_buf(S, (T)->s, (T)->l)
#define n_str_add_cp(S, CP)      n_str_add_buf(S, CP, (CP) ? strlen(CP) : 0)

#ifdef HAVE_DEBUG
# define n_str_dup(S,T)          n_str_dup(S, T, __FILE__, __LINE__)
# define n_str_add_buf(S,B,BL)   n_str_add_buf(S, B, BL, __FILE__, __LINE__)
#endif

/* Our iconv(3) wrappers */

#ifdef HAVE_ICONV
FL iconv_t     n_iconv_open(char const *tocode, char const *fromcode);
/* If *cd* == *iconvd*, assigns -1 to the latter */
FL void        n_iconv_close(iconv_t cd);

/* Reset encoding state */
#ifdef notyet
FL void        n_iconv_reset(iconv_t cd);
#endif

/* iconv(3), but return *errno* or 0; *skipilseq* forces step over illegal byte
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

/* Return wether user says yes.  If prompt is NULL, "Continue (y/n)? " is used
 * instead.  If interactive, asks on STDIN, anything but [0]==[yY] is false.
 * If noninteractive, returns noninteract_default */
FL bool_t      getapproval(char const *prompt, bool_t noninteract_default);

/* [Yy]es or [Nn]o.  Always `yes' if not interactive, always `no' on error */
FL bool_t      yorn(char const *msg);

/* Get a password the expected way, return termios_state.ts_linebuf on
 * success or NULL on error */
FL char *      getuser(char const *query);

/* Get a password the expected way, return termios_state.ts_linebuf on
 * success or NULL on error.
 * termios_state_reset() (def.h) must be called anyway */
FL char *      getpassword(char const *query);

/* Get both, user and password in the expected way; simply reuses a value that
 * is set, otherwise calls one of the above.
 * Returns true only if we have a user and a password.
 * *user* will be savestr()ed if neither it nor *pass* have a default value
 * (so that termios_state.ts_linebuf carries only one) */
FL bool_t      getcredentials(char **user, char **pass);

/* Overall interactive terminal life cycle for command line editor library */
#if defined HAVE_EDITLINE || defined HAVE_READLINE
# define TTY_WANTS_SIGWINCH
#endif
FL void        tty_init(void);
FL void        tty_destroy(void);

/* Rather for main.c / SIGWINCH interaction only */
FL void        tty_signal(int sig);

/* Read a line after printing prompt (if set and non-empty).
 * If n>0 assumes that *linebuf has n bytes of default content */
FL int         tty_readline(char const *prompt, char **linebuf,
                  size_t *linesize, size_t n SMALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define tty_readline(A,B,C,D)   tty_readline(A, B, C, D, __FILE__, __LINE__)
#endif

/* Add a line (most likely as returned by tty_readline()) to the history
 * (only added for real if non-empty and doesn't begin with U+0020) */
FL void        tty_addhist(char const *s);

#if defined HAVE_HISTORY &&\
   (defined HAVE_READLINE || defined HAVE_EDITLINE || defined HAVE_NCL)
FL int         c_history(void *v);
#endif

#ifndef HAVE_AMALGAMATION
# undef FL
# define FL
#endif

/* vim:set fenc=utf-8:s-it-mode */
