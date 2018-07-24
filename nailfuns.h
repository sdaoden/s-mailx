/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Function prototypes and function-alike macros.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2018 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifdef HAVE_MEMORY_DEBUG
# define n_MEMORY_DEBUG_ARGS     , char const *mdbg_file, int mdbg_line
# define n_MEMORY_DEBUG_ARGSCALL , mdbg_file, mdbg_line
#else
# define n_MEMORY_DEBUG_ARGS
# define n_MEMORY_DEBUG_ARGSCALL
#endif

/*
 * Macro-based generics
 */

/* ASCII char classification */
#define n__ischarof(C, FLAGS)  \
   (asciichar(C) && (n_class_char[(ui8_t)(C)] & (FLAGS)) != 0)

#define n_uasciichar(U) ((size_t)(U) <= 0x7F)
#define asciichar(c) ((uc_i)(c) <= 0x7F)
#define alnumchar(c) n__ischarof(c, C_DIGIT | C_OCTAL | C_UPPER | C_LOWER)
#define alphachar(c) n__ischarof(c, C_UPPER | C_LOWER)
#define blankchar(c) n__ischarof(c, C_BLANK)
#define blankspacechar(c) n__ischarof(c, C_BLANK | C_SPACE)
#define cntrlchar(c) n__ischarof(c, C_CNTRL)
#define digitchar(c) n__ischarof(c, C_DIGIT | C_OCTAL)
#define lowerchar(c) n__ischarof(c, C_LOWER)
#define punctchar(c) n__ischarof(c, C_PUNCT)
#define spacechar(c) n__ischarof(c, C_BLANK | C_SPACE | C_WHITE)
#define upperchar(c) n__ischarof(c, C_UPPER)
#define whitechar(c) n__ischarof(c, C_BLANK | C_WHITE)
#define octalchar(c) n__ischarof(c, C_OCTAL)
#define hexchar(c) /* TODO extend bits, add C_HEX */\
   (n__ischarof(c, C_DIGIT | C_OCTAL) || ((c) >= 'A' && (c) <= 'F') ||\
    ((c) >= 'a' && (c) <= 'f'))

#define upperconv(c) \
   (lowerchar(c) ? (char)((uc_i)(c) - 'a' + 'A') : (char)(c))
#define lowerconv(c) \
   (upperchar(c) ? (char)((uc_i)(c) - 'A' + 'a') : (char)(c))
/* RFC 822, 3.2. */
#define fieldnamechar(c) \
   (asciichar(c) && (c) > 040 && (c) != 0177 && (c) != ':')

/* Could the string contain a regular expression?
 * NOTE: on change: manual contains several occurrences of this string! */
#define n_is_maybe_regex(S) n_is_maybe_regex_buf(S, UIZ_MAX)
#define n_is_maybe_regex_buf(D,L) n_anyof_buf("^[]*+?|$", D, L)

/* Single-threaded, use unlocked I/O */
#ifdef HAVE_PUTC_UNLOCKED
# undef getc
# define getc(c)        getc_unlocked(c)
# undef putc
# define putc(c, f)     putc_unlocked(c, f)
#endif

/* There are problems with dup()ing of file-descriptors for child processes.
 * We have to somehow accomplish that the FILE* fp makes itself comfortable
 * with the *real* offset of the underlaying file descriptor.
 * POSIX Issue 7 overloaded fflush(3): if used on a readable stream, then
 *
 *    if the file is not already at EOF, and the file is one capable of
 *    seeking, the file offset of the underlying open file description shall
 *    be set to the file position of the stream */
#if defined _POSIX_VERSION && _POSIX_VERSION + 0 >= 200809L
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

# define n_fd_cloexec_set(FD) \
do{\
      int a__fd = (FD)/*, a__fl*/;\
      /*if((a__fl = fcntl(a__fd, F_GETFD)) != -1 && !(a__fl & FD_CLOEXEC))*/\
         (void)fcntl(a__fd, F_SETFD, FD_CLOEXEC);\
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
FL int c_account(void *v);
FL int c_unaccount(void *v);

/* `localopts', `shift', `return' */
FL int c_localopts(void *vp);
FL int c_shift(void *vp);
FL int c_return(void *vp);

/* TODO Check whether a *folder-hook* exists for the currently active mailbox */
FL bool_t temporary_folder_hook_check(bool_t nmail);
FL void temporary_folder_hook_unroll(void); /* XXX im. hack */

/* TODO v15 drop Invoke compose hook macname */
FL void temporary_compose_mode_hook_call(char const *macname,
            void (*hook_pre)(void *), void *hook_arg);
FL void temporary_compose_mode_hook_unroll(void);

/* Can name freely be used as a variable by users? */
FL bool_t n_var_is_user_writable(char const *name);

/* Don't use n_var_* unless you *really* have to! */

/* Constant option key look/(un)set/clear */
FL char *n_var_oklook(enum okeys okey);
#define ok_blook(C) (n_var_oklook(n_CONCAT(ok_b_, C)) != NULL)
#define ok_vlook(C) n_var_oklook(n_CONCAT(ok_v_, C))

FL bool_t n_var_okset(enum okeys okey, uintptr_t val);
#define ok_bset(C) \
   n_var_okset(n_CONCAT(ok_b_, C), (uintptr_t)TRU1)
#define ok_vset(C,V) \
   n_var_okset(n_CONCAT(ok_v_, C), (uintptr_t)(V))

FL bool_t n_var_okclear(enum okeys okey);
#define ok_bclear(C) n_var_okclear(n_CONCAT(ok_b_, C))
#define ok_vclear(C) n_var_okclear(n_CONCAT(ok_v_, C))

/* Variable option key lookup/(un)set/clear.
 * If try_getenv is true we'll getenv(3) _if_ vokey is not a known enum okey.
 * _vexplode() is to be used by the shell expansion stuff when encountering
 * ${@} in double-quotes, in order to provide sh(1)ell compatible behaviour;
 * it returns whether there are any elements in argv (*cookie) */
FL char const *n_var_vlook(char const *vokey, bool_t try_getenv);
FL bool_t n_var_vexplode(void const **cookie);
FL bool_t n_var_vset(char const *vokey, uintptr_t val);
FL bool_t n_var_vclear(char const *vokey);

/* Special case to handle the typical [xy-USER@HOST,] xy-HOST and plain xy
 * variable chains; oxm is a bitmix which tells which combinations to test */
#ifdef HAVE_SOCKETS
FL char *n_var_xoklook(enum okeys okey, struct url const *urlp,
            enum okey_xlook_mode oxm);
# define xok_BLOOK(C,URL,M) (n_var_xoklook(C, URL, M) != NULL)
# define xok_VLOOK(C,URL,M) n_var_xoklook(C, URL, M)
# define xok_blook(C,URL,M) xok_BLOOK(n_CONCAT(ok_b_, C), URL, M)
# define xok_vlook(C,URL,M) xok_VLOOK(n_CONCAT(ok_v_, C), URL, M)
#endif

/* User variable access: `set', `local' and `unset' */
FL int c_set(void *vp);
FL int c_unset(void *vp);

/* `varshow' */
FL int c_varshow(void *v);

/* Ditto: `varedit' */
FL int c_varedit(void *v);

/* `environ' */
FL int c_environ(void *v);

/* `vexpr' */
FL int c_vexpr(void *v);

/* `vpospar' */
FL int c_vpospar(void *v);

/*
 * attachment.c
 * xxx Interface quite sick
 */

/* Try to add an attachment for file, fexpand(_LOCAL|_NOPROTO)ed.
 * Return the new aplist aphead.
 * The newly created attachment may be stored in *newap, or NULL on error */
FL struct attachment *n_attachment_append(struct attachment *aplist,
                        char const *file, enum n_attach_error *aerr_or_null,
                        struct attachment **newap_or_null);

/* Shell-token parse names, and append resulting file names to aplist, return
 * (new) aplist head */
FL struct attachment *n_attachment_append_list(struct attachment *aplist,
                        char const *names);

/* Remove ap from aplist, and return the new aplist head */
FL struct attachment *n_attachment_remove(struct attachment *aplist,
                        struct attachment *ap);

/* Find by file-name.  If any path component exists in name then an exact match
 * of the creation-path is used directly; if instead the basename of that path
 * matches all attachments are traversed to find an exact match first, the
 * first of all basename matches is returned as a last resort;
 * If no path component exists the filename= parameter is searched (and also
 * returned) in preference over the basename, otherwise likewise.
 * If name is in fact a message number the first match is taken.
 * If stat_or_null is given: FAL0 on NULL return, TRU1 for exact/single match,
 * TRUM1 for ambiguous matches */
FL struct attachment *n_attachment_find(struct attachment *aplist,
                        char const *name, bool_t *stat_or_null);

/* Interactively edit the attachment list, return updated list */
FL struct attachment *n_attachment_list_edit(struct attachment *aplist,
                        enum n_go_input_flags gif);

/* Print all attachments to fp, return number of lines written, -1 on error */
FL ssize_t n_attachment_list_print(struct attachment const *aplist, FILE *fp);

/*
 * auxlily.c
 */

/* setlocale(3), *ttycharset* etc. */
FL void n_locale_init(void);

/* Compute screen size */
FL size_t n_screensize(void);

/* Get our $PAGER; if env_addon is not NULL it is checked whether we know about
 * some environment variable that supports colour+ and set *env_addon to that,
 * e.g., "LESS=FRSXi" */
FL char const *n_pager_get(char const **env_addon);

/* Use a pager or STDOUT to print *fp*; if *lines* is 0, they'll be counted */
FL void        page_or_print(FILE *fp, size_t lines);

/* Parse name and guess at the required protocol.
 * If check_stat is true then stat(2) will be consulted - a TODO c..p hack
 * TODO that together with *newfolders*=maildir adds Maildir support; sigh!
 * If try_hooks is set check_stat is implied and if the stat(2) fails all
 * file-hook will be tried in order to find a supported version of name.
 * If adjusted_or_null is not NULL it will be set to the final version of name
 * this function knew about: a %: FEDIT_SYSBOX prefix is forgotten, in case
 * a hook is needed the "real" filename will be placed.
 * TODO This c..p should be URL::from_string()->protocol() or something! */
FL enum protocol  which_protocol(char const *name, bool_t check_stat,
                     bool_t try_hooks, char const **adjusted_or_null);

/* Hexadecimal itoa (NUL terminates) / atoi (-1 on error) */
FL char *      n_c_to_hex_base16(char store[3], char c);
FL si32_t      n_c_from_hex_base16(char const hex[2]);

/* Decode clen (or strlen() if UIZ_MAX) bytes of cbuf into an integer
 * according to idm, store a/the result in *resp (in _EINVAL case an overflow
 * constant is assigned, for signed types it depends on parse state w. MIN/MAX),
 * which must point to storage of the correct type, and return the result state.
 * If endptr_or_null is set it will be set to the byte where parsing stopped */
FL enum n_idec_state n_idec_buf(void *resp, char const *cbuf, uiz_t clen,
                        ui8_t base, enum n_idec_mode idm,
                        char const **endptr_or_null);
#define n_idec_cp(RP,CBP,B,M,CLP) n_idec_buf(RP, CBP, UIZ_MAX, B, M, CLP)

#define n_idec_ui8_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B, (n_IDEC_MODE_LIMIT_8BIT), CLP)
#define n_idec_si8_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B,\
      (n_IDEC_MODE_SIGNED_TYPE | n_IDEC_MODE_LIMIT_8BIT), CLP)
#define n_idec_ui16_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B, (n_IDEC_MODE_LIMIT_16BIT), CLP)
#define n_idec_si16_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B,\
      (n_IDEC_MODE_SIGNED_TYPE | n_IDEC_MODE_LIMIT_16BIT), CLP)
#define n_idec_ui32_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B, (n_IDEC_MODE_LIMIT_32BIT), CLP)
#define n_idec_si32_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B,\
      (n_IDEC_MODE_SIGNED_TYPE | n_IDEC_MODE_LIMIT_32BIT), CLP)
#define n_idec_ui64_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B, 0, CLP)
#define n_idec_si64_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B, (n_IDEC_MODE_SIGNED_TYPE), CLP)
#if UIZ_MAX == UI32_MAX
# define n_idec_uiz_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B, (n_IDEC_MODE_LIMIT_32BIT), CLP)
# define n_idec_siz_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B,\
      (n_IDEC_MODE_SIGNED_TYPE | n_IDEC_MODE_LIMIT_32BIT), CLP)
#else
# define n_idec_uiz_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B, 0, CLP)
# define n_idec_siz_cp(RP,CBP,B,CLP) \
   n_idec_buf(RP, CBP, UIZ_MAX, B, (n_IDEC_MODE_SIGNED_TYPE), CLP)
#endif

/* Encode an integer value according to base (2-36) and mode iem, return
 * pointer to starting byte or NULL on error */
FL char *n_ienc_buf(char cbuf[n_IENC_BUFFER_SIZE], ui64_t value, ui8_t base,
            enum n_ienc_mode iem);

/* Hash the passed string -- uses Chris Torek's hash algorithm.
 * i*() hashes case-insensitively (ASCII), and *n() uses maximally len bytes;
 * if len is UIZ_MAX, we go .), since we anyway stop for NUL */
FL ui32_t n_torek_hash(char const *name);
FL ui32_t n_torek_ihashn(char const *dat, size_t len);
#define n_torek_ihash(CP) n_torek_ihashn(CP, UIZ_MAX)

/* Find a prime greater than n */
FL ui32_t n_prime_next(ui32_t n);

/* Return the name of the dead.letter file */
FL char const * n_getdeadletter(void);

/* Detect and query the hostname to use */
FL char *n_nodename(bool_t mayoverride);

/* Convert from / to *ttycharset* */
#ifdef HAVE_IDNA
FL bool_t n_idna_to_ascii(struct n_string *out, char const *ibuf, size_t ilen);
/*TODO FL bool_t n_idna_from_ascii(struct n_string *out, char const *ibuf,
            size_t ilen);*/
#endif

/* Get a (pseudo) random string of *len* bytes, _not_ counting the NUL
 * terminator, the second returns an n_autorec_alloc()ed buffer.
 * If n_PSO_REPRODUCIBLE and reprocnt_or_null not NULL then we produce
 * a reproducable string by using and managing that counter instead */
FL char *n_random_create_buf(char *dat, size_t len, ui32_t *reprocnt_or_null);
FL char *n_random_create_cp(size_t len, ui32_t *reprocnt_or_null);

/* Check whether the argument string is a TRU1 or FAL0 boolean, or an invalid
 * string, in which case TRUM1 is returned.
 * If the input buffer is empty emptyrv is used as the return: if it is GE
 * FAL0 it will be made a binary boolean, otherwise TRU2 is returned.
 * inlen may be UIZ_MAX to force strlen() detection */
FL bool_t n_boolify(char const *inbuf, uiz_t inlen, bool_t emptyrv);

/* Dig a "quadoption" in inbuf, possibly going through getapproval() in
 * interactive mode, in which case prompt is printed first if set.
.  Just like n_boolify() otherwise */
FL bool_t n_quadify(char const *inbuf, uiz_t inlen, char const *prompt,
            bool_t emptyrv);

/* Is the argument "all" (case-insensitive) or "*" */
FL bool_t n_is_all_or_aster(char const *name);

/* Get seconds since epoch, return pointer to static struct.
 * Unless force_update is true we may use the event-loop tick time */
FL struct n_timespec const *n_time_now(bool_t force_update);
#define n_time_epoch() ((time_t)n_time_now(FAL0)->ts_sec)

/* Update *tc* to now; only .tc_time updated unless *full_update* is true */
FL void        time_current_update(struct time_current *tc,
                  bool_t full_update);

/* ctime(3), but do ensure 26 byte limit, do not crash XXX static buffer.
 * NOTE: no trailing newline */
FL char *n_time_ctime(si64_t secsepoch, struct tm const *localtime_or_nil);

/* Returns 0 if fully slept, number of millis left if ignint is true and we
 * were interrupted.  Actual resolution may be second or less.
 * Note in case of HAVE_SLEEP this may be SIGALARM based. */
FL uiz_t n_msleep(uiz_t millis, bool_t ignint);

/* Our error print series..  Note: these reverse scan format in order to know
 * whether a newline was included or not -- this affects the output! */
FL void        n_err(char const *format, ...);
FL void        n_verr(char const *format, va_list ap);

/* ..(for use in a signal handler; to be obsoleted..).. */
FL void        n_err_sighdl(char const *format, ...);

/* Our perror(3); if errval is 0 n_err_no is used; newline appended */
FL void        n_perr(char const *msg, int errval);

/* Announce a fatal error (and die); newline appended */
FL void        n_alert(char const *format, ...);
FL void        n_panic(char const *format, ...);

/* `errors' */
#ifdef HAVE_ERRORS
FL int c_errors(void *vp);
#endif

/* strerror(3), and enum n_err_number <-> error name conversions */
FL char const *n_err_to_doc(si32_t eno);
FL char const *n_err_to_name(si32_t eno);
FL si32_t n_err_from_name(char const *name);

/* */
#ifdef HAVE_REGEX
FL char const *n_regex_err_to_doc(const regex_t *rep, int e);
#endif

/*
 * cmd-cnd.c
 */

/* if.elif.else.endif conditional execution.
 * _isskip() tests whether current state doesn't allow execution of commands */
FL int c_if(void *v);
FL int c_elif(void *v);
FL int c_else(void *v);
FL int c_endif(void *v);

FL bool_t n_cnd_if_isskip(void);

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
FL void print_headers(int const *msgvec, bool_t only_marked,
         bool_t subject_thread_compress);

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

/* `sleep' */
FL int c_sleep(void *v);

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

/* `read' */
FL int c_read(void *vp);

/* `readall' */
FL int c_readall(void *vp);

/* `version' */
FL int c_version(void *vp);

/*
 * cmd-resend.c
 */

/* All thinkable sorts of `reply' / `respond' and `followup'.. */
FL int c_reply(void *vp);
FL int c_replyall(void *vp);
FL int c_replysender(void *vp);
FL int c_Reply(void *vp);
FL int c_followup(void *vp);
FL int c_followupall(void *vp);
FL int c_followupsender(void *vp);
FL int c_Followup(void *vp);

/* ..and a mailing-list reply */
FL int c_Lreply(void *vp);

/* 'forward' / `Forward' */
FL int c_forward(void *vp);
FL int c_Forward(void *vp);

/* Resend a message list to a third person.
 * The latter does not add the Resent-* header series */
FL int c_resend(void *vp);
FL int c_Resend(void *vp);

/*
 * cmd-tab.c
 * Actual command table, `help', `list', etc., and the n_cmd_arg() parser.
 */

/* Isolate the command from the arguments, return pointer to end of cmd name */
FL char const *n_cmd_isolate(char const *cmd);

/* First command which fits for cmd, or NULL */
FL struct n_cmd_desc const *n_cmd_firstfit(char const *cmd);

/* Get the default command for the empty line */
FL struct n_cmd_desc const *n_cmd_default(void);

/* Scan an entire command argument line, return whether result can be used,
 * otherwise no resources are allocated (in ->cac_arg).
 * For _WYSH arguments the flags _TRIM_SPACE (v15 _not_ _TRIM_IFSSPACE) and
 * _LOG are implicit, _META_SEMICOLON is starting with the last (non-optional)
 * argument, and then a trailing empty argument is ignored, too */
FL bool_t n_cmd_arg_parse(struct n_cmd_arg_ctx *cacp);

/* Save away the data from autorec memory, and restore it to that.
 * The heap storage is a single pointer to be n_free() by users */
FL void *n_cmd_arg_save_to_heap(struct n_cmd_arg_ctx const *cacp);
FL struct n_cmd_arg_ctx *n_cmd_arg_restore_from_heap(void *vp);

/* Scan out the list of string arguments according to rm, return -1 on error;
 * res_dat is NULL terminated unless res_size is 0 or error occurred */
FL int /* TODO v15*/ getrawlist(bool_t wysh, char **res_dat, size_t res_size,
                  char const *line, size_t linesize);

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
            struct message *mp, char const *quotefile, si8_t *checkaddr_err);

/*
 * colour.c
 */

#ifdef HAVE_COLOUR
/* `(un)?colour' */
FL int c_colour(void *v);
FL int c_uncolour(void *v);

/* An execution context is teared down, and it finds to have a colour stack.
 * Signals are blocked */
FL void n_colour_stack_del(struct n_go_data_ctx *gdcp);

/* We want coloured output (in this autorec memory() cycle), pager_used is used
 * to test whether *colour-pager* is to be inspected, if fp is given, the reset
 * sequence will be written as necessary by _stack_del()
 * env_gut() will reset() as necessary if fp is not NULL */
FL void n_colour_env_create(enum n_colour_ctx cctx, FILE *fp,
         bool_t pager_used);
FL void n_colour_env_gut(void);

/* Putting anything (for pens: including NULL) resets current state first */
FL void n_colour_put(enum n_colour_id cid, char const *ctag);
FL void n_colour_reset(void);

/* Of course temporary only and may return NULL.  Doesn't affect state! */
FL struct str const *n_colour_reset_to_str(void);

/* A pen is bound to its environment and automatically reclaimed; it may be
 * NULL but that can be used anyway for simplicity.
 * This includes pen_to_str() -- which doesn't affect state! */
FL struct n_colour_pen *n_colour_pen_create(enum n_colour_id cid,
                           char const *ctag);
FL void n_colour_pen_put(struct n_colour_pen *self);

FL struct str const *n_colour_pen_to_str(struct n_colour_pen *self);
#endif /* HAVE_COLOUR */

/*
 * dotlock.c
 */

/* Aquire a flt n_file_lock().
 * Will try FILE_LOCK_TRIES times if pollmsecs > 0 (once otherwise).
 * If pollmsecs is UIZ_MAX, FILE_LOCK_MILLIS is used.
 * If *dotlock-disable* is set (FILE*)-1 is returned if flt could be aquired,
 * NULL if not, with n_err_ being usable.
 * Otherwise a dotlock file is created, and a registered control-pipe FILE* is
 * returned upon success which keeps the link in between us and the
 * lock-holding fork(2)ed subprocess (which conditionally replaced itself via
 * execv(2) with the privilege-separated dotlock helper program): the lock file
 * will be removed once the control pipe is closed via Pclose().
 * If *dotlock_ignore_error* is set (FILE*)-1 will be returned if at least the
 * normal file lock could be established, otherwise n_err_no is usable on err */
FL FILE *n_dotlock(char const *fname, int fd, enum n_file_lock_type flt,
            off_t off, off_t len, size_t pollmsecs);

/*
 * edit.c
 */

/* Edit a message list */
FL int         c_editor(void *v);

/* Invoke the visual editor on a message list */
FL int         c_visual(void *v);

/* Run an editor on either size bytes of the file fp (or until EOF if size is
 * negative) or on the message mp, and return a new file or NULL on error of if
 * the user didn't perform any edits (not possible in pipe mode).
 * For now we assert that mp==NULL if hp!=NULL, treating this as a special call
 * from within compose mode, and giving TRUM1 to n_puthead().
 * Signals must be handled by the caller.
 * viored is 'e' for $EDITOR, 'v' for $VISUAL, or '|' for n_child_run(), in
 * which case pipecmd must have been given */
FL FILE *n_run_editor(FILE *fp, off_t size, int viored, bool_t readonly,
                  struct header *hp, struct message *mp,
                  enum sendaction action, sighandler_type oldint,
                  char const *pipecmd);

/*
 * filter.c
 */

/* Quote filter */
FL struct quoteflt * quoteflt_dummy(void); /* TODO LEGACY */
FL void        quoteflt_init(struct quoteflt *self, char const *prefix,
                  bool_t bypass);
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
 * Manages the n_PS_READLINE_NL hack */
FL char *      fgetline(char **line, size_t *linesize, size_t *count,
                  size_t *llen, FILE *fp, int appendnl n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define fgetline(A,B,C,D,E,F)   \
   fgetline(A, B, C, D, E, F, __FILE__, __LINE__)
#endif

/* Read up a line from the specified input into the linebuffer.
 * Return the number of characters read.  Do not include the newline at EOL.
 * n is the number of characters already read and present in *linebuf; it'll be
 * treated as _the_ line if no more (successful) reads are possible.
 * Manages the n_PS_READLINE_NL hack */
FL int         readline_restart(FILE *ibuf, char **linebuf, size_t *linesize,
                  size_t n n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define readline_restart(A,B,C,D) \
   readline_restart(A, B, C, D, __FILE__, __LINE__)
#endif

/* Determine the size of the file possessed by the passed buffer */
FL off_t       fsize(FILE *iob);

/* Will retry FILE_LOCK_RETRIES times if pollmsecs > 0.
 * If pollmsecs is UIZ_MAX, FILE_LOCK_MILLIS is used */
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
 * Returns n_err_no of error */
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
FL bool_t n_go_main_loop(void);

/* Actual cmd input */

/* */
FL void n_go_input_clearerr(void);

/* Force n_go_input() to read EOF next */
FL void n_go_input_force_eof(void);

/* Returns true if force_eof() has been set -- it is set automatically if
 * an input context enters EOF state (rather than error, as in ferror(3)) */
FL bool_t n_go_input_is_eof(void);

/* Force n_go_input() to read that buffer next -- for `history', and the MLE.
 * If commit is not true then we'll reenter the line editor with buf as default
 * line content.  Only to be used in interactive and non-robot mode! */
FL void n_go_input_inject(enum n_go_input_inject_flags giif, char const *buf,
            size_t len);

/* Read a complete line of input, with editing if interactive and possible.
 * If string is set it is used as the initial line content if in interactive
 * mode, otherwise this argument is ignored for reproducibility.
 * If histok_or_null is set it will be updated to FAL0 if input shall not be
 * placed in history.
 * Return number of octets or a value <0 on error.
 * Note: may use the currently `source'd file stream instead of stdin!
 * Manages the n_PS_READLINE_NL hack
 * TODO We need an OnReadLineCompletedEvent and drop this function */
FL int n_go_input(enum n_go_input_flags gif, char const *prompt,
         char **linebuf, size_t *linesize, char const *string,
         bool_t *histok_or_null n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define n_go_input(A,B,C,D,E,F) n_go_input(A,B,C,D,E,F,__FILE__,__LINE__)
#endif

/* Read a line of input, with editing if interactive and possible, return it
 * savestr()d or NULL in case of errors or if an empty line would be returned.
 * This may only be called from toplevel (not during n_PS_ROBOT).
 * If string is set it is used as the initial line content if in interactive
 * mode, otherwise this argument is ignored for reproducibility */
FL char *n_go_input_cp(enum n_go_input_flags gif, char const *prompt,
            char const *string);

/* Deal with loading of resource files and dealing with a stack of files for
 * the source command */

/* Load a file of user system startup resources.
 * *Only* for main(), returns whether program shall continue */
FL bool_t n_go_load(char const *name);

/* "Load" all the -X command line definitions in order.
 * *Only* for main(), returns whether program shall continue */
FL bool_t n_go_Xargs(char const **lines, size_t cnt);

/* Pushdown current input file and switch to a new one.  Set the global flag
 * n_PS_SOURCING so that others will realize that they are no longer reading
 * from a tty (in all probability) */
FL int c_source(void *v);
FL int c_source_if(void *v);

/* Evaluate a complete macro / a single command.  For the former lines will
 * always be free()d, for the latter cmd will always be duplicated internally */
FL bool_t n_go_macro(enum n_go_input_flags gif, char const *name, char **lines,
            void (*on_finalize)(void*), void *finalize_arg);
FL bool_t n_go_command(enum n_go_input_flags gif, char const *cmd);

/* XXX See a_GO_SPLICE in source */
FL void n_go_splice_hack(char const *cmd, FILE *new_stdin, FILE *new_stdout,
         ui32_t new_psonce, void (*on_finalize)(void*), void *finalize_arg);
FL void n_go_splice_hack_remove_after_jump(void);

/* XXX Hack: may we release our (interactive) (terminal) control to a different
 * XXX program, e.g., a $PAGER? */
FL bool_t n_go_may_yield_control(void);

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
FL bool_t      is_head(char const *linebuf, size_t linelen,
                  bool_t check_rfc4155);

/* Print hp "to user interface" fp for composing purposes xxx what a sigh */
FL bool_t n_header_put4compose(FILE *fp, struct header *hp);

/* Extract some header fields (see e.g. -t documentation) from a message.
 * If extended_list_of is set a number of additional header fields are
 * understood and address joining is performed as necessary, and the subject
 * is treated with additional care, too;
 * if it is set to TRUM1 then From: and Sender: will not be assigned no more,
 * if it is TRU1 then to,cc,bcc present in hp will be used to prefill the new
 * header; in any case a true boolean causes shell comments to be understood.
 * This calls expandaddr() on some headers and sets checkaddr_err if that is
 * not NULL -- note it explicitly allows EAF_NAME because aliases are not
 * expanded when this is called! */
FL void n_header_extract(FILE *fp, struct header *hp, bool_t extended_list_of,
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

/* Start of a "comment".  Ignore it */
FL char const * skip_comment(char const *cp);

/* Return the start of a route-addr (address in angle brackets), if present */
FL char const * routeaddr(char const *name);

/* Query *expandaddr*, parse it and return flags.
 * The flags are already adjusted for n_PSO_INTERACTIVE, n_PO_TILDE_FLAG etc. */
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
 * Return NULL on error, or name again, but which may have been replaced by
 * a version with fixed quotation etc.!
 * issingle_hack is a HACK that allows usage for `addrcodec' */
FL char const *n_addrspec_with_guts(struct n_addrguts *agp, char const *name,
                  bool_t doskin, bool_t issingle_hack);

/* Fetch the real name from an internet mail address field */
FL char *      realname(char const *name);

/* Get the list of senders (From: or Sender: or From_ line) from this message.
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
#if defined HAVE_IMAP_SEARCH || defined HAVE_IMAP
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
 * and the full names in *name_full_or_null, taking acount for *showname*.
 * If *is_to_or_null is set, *showto* and n_is_myname() are taken into account
 * when choosing which names to use.
 * The list as such is returned, or NULL if there is really none (empty strings
 * will be stored, then).
 * All results are in auto-reclaimed storage, but may point to the same string.
 * TODO *is_to_or_null could be set to whether we actually used To:, or not.
 * TODO n_header_textual_sender_info(): should only create a list of matching
 * TODO name objects, which the user can iterate over and o->to_str().. */
FL struct name *n_header_textual_sender_info(struct message *mp,
                  char **cumulation_or_null, char **addr_or_null,
                  char **name_real_or_null, char **name_full_or_null,
                  bool_t *is_to_or_null);

/* TODO Weird thing that tries to fill in From: and Sender: */
FL void        setup_from_and_sender(struct header *hp);

/* Note: returns 0x1 if both args were NULL */
FL struct name const * check_from_and_sender(struct name const *fromfield,
                        struct name const *senderfield);

#ifdef HAVE_XTLS
FL char *      getsender(struct message *m);
#endif

/* This returns NULL if hp is NULL or when no information is available.
 * hp remains unchanged (->h_in_reply_to is not set!)  */
FL struct name *n_header_setup_in_reply_to(struct header *hp);

/* Fill in / reedit the desired header fields */
FL int         grab_headers(enum n_go_input_flags gif, struct header *hp,
                  enum gfield gflags, int subjfirst);

/* Check whether sep->ss_sexpr (or ->ss_sregex) matches any header of mp.
 * If sep->s_where (or >s_where_wregex) is set, restrict to given headers */
FL bool_t n_header_match(struct message *mp, struct search_expr const *sep);

/* Verify whether len (UIZ_MAX=strlen) bytes of name form a standard or
 * otherwise known header name (that must not be used as a custom header).
 * Return the (standard) header name, or NULL */
FL char const *n_header_is_known(char const *name, size_t len);

/* Add a custom header to the given list, in auto-reclaimed or heap memory */
FL bool_t n_header_add_custom(struct n_header_field **hflp, char const *dat,
            bool_t heap);

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
FL struct n_ignore *n_ignore_new(bool_t isauto);
FL void n_ignore_del(struct n_ignore *self);

/* Are there just _any_ user settings covered by self? */
FL bool_t n_ignore_is_any(struct n_ignore const *self);

/* Set an entry to retain (or ignore).
 * Returns FAL0 if dat is not a valid header field name or an invalid regular
 * expression, TRU1 if insertion took place, and TRUM1 if already set */
FL bool_t n_ignore_insert(struct n_ignore *self, bool_t retain,
            char const *dat, size_t len);
#define n_ignore_insert_cp(SELF,RT,CP) n_ignore_insert(SELF, RT, CP, UIZ_MAX)

/* Returns TRU1 if retained, TRUM1 if ignored, FAL0 if not covered */
FL bool_t n_ignore_lookup(struct n_ignore const *self, char const *dat,
            size_t len);
#define n_ignore_lookup_cp(SELF,CP) n_ignore_lookup(SELF, CP, UIZ_MAX)
#define n_ignore_is_ign(SELF,FDAT,FLEN) \
   (n_ignore_lookup(SELF, FDAT, FLEN) == TRUM1)

/*
 * imap-search.c
 */

/* Return -1 on invalid spec etc., the number of matches otherwise */
#ifdef HAVE_IMAP_SEARCH
FL ssize_t     imap_search(char const *spec, int f);
#endif

/*
 * maildir.c
 */

#ifdef HAVE_MAILDIR
FL int maildir_setfile(char const *who, char const *name, enum fedit_mode fm);

FL bool_t maildir_quit(bool_t hold_sigs_on);

FL enum okay maildir_append(char const *name, FILE *fp, long offset);

FL enum okay maildir_remove(char const *name);
#endif /* HAVE_MAILDIR */

/*
 * memory.c
 * Heap memory and automatically reclaimed storage, plus pseudo "alloca"
 */

/* Called from the (main)loops upon tick and break-off time to perform debug
 * checking and memory cleanup, including stack-top of auto-reclaimed storage */
FL void n_memory_reset(void);

/* Fixate the current snapshot of our global auto-reclaimed storage instance,
 * so that further allocations become auto-reclaimed.
 * This is only called from main.c for the global arena */
FL void n_memory_pool_fixate(void);

/* Lifetime management of a per-execution level arena (to be found in
 * n_go_data_ctx.gdc_mempool, lazy initialized).
 * _push() can be used by anyone to create a new stack layer in the current
 * per-execution level arena, which is layered upon the normal one (usually
 * provided by .gdc__mempool_buf, initialized as necessary).
 * This can be pop()ped again: popping a stack will remove all stacks "above"
 * it, i.e., those which have been pushed thereafter.
 * If NULL is popped then this means that the current per-execution level is
 * left and n_go_data_ctx.gdc_mempool is not NULL; an event loop tick also
 * causes all _push()ed stacks to be dropped (via n_memory_reset()) */
FL void n_memory_pool_push(void *vp);
FL void n_memory_pool_pop(void *vp);

/* Generic heap memory */

FL void *n_alloc(size_t s n_MEMORY_DEBUG_ARGS);
FL void *n_realloc(void *v, size_t s n_MEMORY_DEBUG_ARGS);
FL void *n_calloc(size_t nmemb, size_t size n_MEMORY_DEBUG_ARGS);
FL void n_free(void *vp n_MEMORY_DEBUG_ARGS);

/* TODO obsolete c{m,re,c}salloc -> n_* */
#ifdef HAVE_MEMORY_DEBUG
# define n_alloc(S) (n_alloc)(S, __FILE__, __LINE__)
# define n_realloc(P,S) (n_realloc)(P, S, __FILE__, __LINE__)
# define n_calloc(N,S) (n_calloc)(N, S, __FILE__, __LINE__)
# define n_free(P) (n_free)(P, __FILE__, __LINE__)
#else
# define n_free(P) free(P)
#endif

/* Fluctuating heap memory (supposed to exist for one command loop tick) */

#define n_flux_alloc(S) n_alloc(S)
#define n_flux_realloc(P,S) n_realloc(P, S)
#define n_flux_calloc(N,S) n_calloc(N, S)
#define n_flux_free(P) n_free(P)

/* Auto-reclaimed storage */

/* Lower memory pressure on auto-reclaimed storage for code which has
 * a sinus-curve looking style of memory usage, i.e., peak followed by release,
 * like, e.g., doing a task on all messages of a box in order.
 * Such code should call _create(), successively call _unroll() after
 * a single message has been handled, and finally _gut() */
FL void n_autorec_relax_create(void);
FL void n_autorec_relax_gut(void);
FL void n_autorec_relax_unroll(void);

/* TODO obsolete srelax -> n_autorec_relax_* */
#define srelax_hold() n_autorec_relax_create()
#define srelax_rele() n_autorec_relax_gut()
#define srelax() n_autorec_relax_unroll()

/* Allocate size more bytes of space and return the address of the first byte
 * to the caller.  An even number of bytes are always allocated so that the
 * space will always be on a word boundary */
FL void *n_autorec_alloc_from_pool(void *vp, size_t size n_MEMORY_DEBUG_ARGS);
FL void *n_autorec_calloc_from_pool(void *vp, size_t nmemb, size_t size
            n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define n_autorec_alloc_from_pool(VP,SZ) \
   (n_autorec_alloc_from_pool)(VP, SZ, __FILE__, __LINE__)
# define n_autorec_calloc_from_pool(VP,NM,SZ) \
   (n_autorec_calloc_from_pool)(VP, NM, SZ, __FILE__, __LINE__)
#endif
#define n_autorec_alloc(SZ) n_autorec_alloc_from_pool(NULL, SZ)
#define n_autorec_calloc(NM,SZ) n_autorec_calloc_from_pool(NULL, NM, SZ)

/* Pseudo alloca (and also auto-reclaimed in _memory_reset()/_pool_pop()) */
FL void *n_lofi_alloc(size_t size n_MEMORY_DEBUG_ARGS);
FL void n_lofi_free(void *vp n_MEMORY_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
# define n_lofi_alloc(SZ) (n_lofi_alloc)(SZ, __FILE__, __LINE__)
# define n_lofi_free(P) (n_lofi_free)(P, __FILE__, __LINE__)
#endif

/* The snapshot can be used in a local context, in order to free many
 * allocations in one go */
FL void *n_lofi_snap_create(void);
FL void n_lofi_snap_unroll(void *cookie);

/* */
#ifdef HAVE_MEMORY_DEBUG
FL int c_memtrace(void *v);

/* For immediate debugging purposes, it is possible to check on request */
FL bool_t n__memory_check(char const *file, int line);
# define n_memory_check() n__memory_check(__FILE__, __LINE__)
#else
# define n_memory_check() do{;}while(0)
#endif

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
FL bool_t      message_match(struct message *mp, struct search_expr const *sep,
               bool_t with_headers);

/*  */
FL struct message * setdot(struct message *mp);

/* Touch the named message by setting its MTOUCH flag.  Touched messages have
 * the effect of not being sent back to the system mailbox on exit */
FL void        touch(struct message *mp);

/* Convert user message spec. to message numbers and store them in vector,
 * which should be capable to hold msgCount+1 entries (n_msgvec asserts this).
 * flags is n_cmd_arg_ctx.cac_msgflag == n_cmd_desc.cd_msgflag == enum mflag.
 * If capp_or_null is not NULL then the last (string) token is stored in here
 * and not interpreted as a message specification; in addition, if only one
 * argument remains and this is the empty string, 0 is returned (*vector=0;
 * this is used to implement n_CMD_ARG_DESC_MSGLIST_AND_TARGET).
 * A NUL *buf input results in a 0 return, *vector=0, [*capp_or_null=NULL].
 * Returns the count of messages picked up or -1 on error */
FL int n_getmsglist(char const *buf, int *vector, int flags,
         struct n_cmd_arg **capp_or_null);

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
 * Without HAVE_ICONV, this "iterates" over *ttycharset* only */
FL bool_t      charset_iter_reset(char const *a_charset_to_try_first);
FL bool_t      charset_iter_next(void);
FL bool_t      charset_iter_is_valid(void);
FL char const * charset_iter(void);

/* And this is (xxx temporary?) which returns the iterator if that is valid and
 * otherwise either *charset-8bit* or *ttycharset*, dep. on HAVE_ICONV */
FL char const * charset_iter_or_fallback(void);

FL void        charset_iter_recurse(char *outer_storage[2]); /* TODO LEGACY */
FL void        charset_iter_restore(char *outer_storage[2]); /* TODO LEGACY */

/* Check whether our headers will need MIME conversion */
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
                  struct quoteflt *qf, struct str *outrest,
                  struct str *inrest);
FL ssize_t     xmime_write(char const *ptr, size_t size, /* TODO LEGACY */
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

/* Default MIME Content-Transfer-Encoding: as via *mime-encoding* */
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
 * Includes room for terminator, UIZ_MAX on overflow */
FL size_t      qp_encode_calc_size(size_t len);

/* If flags includes QP_ISHEAD these assume "word" input and use special
 * quoting rules in addition; soft line breaks are not generated.
 * Otherwise complete input lines are assumed and soft line breaks are
 * generated as necessary.  Return NULL on error (overflow) */
FL struct str * qp_encode(struct str *out, struct str const *in,
                  enum qpflags flags);
#ifdef notyet
FL struct str * qp_encode_cp(struct str *out, char const *cp,
                  enum qpflags flags);
FL struct str * qp_encode_buf(struct str *out, void const *vp, size_t vp_len,
                  enum qpflags flags);
#endif

/* The buffers of out and *rest* will be managed via n_realloc().
 * If inrest_or_null is needed but NULL an error occurs, otherwise tolerant.
 * Return FAL0 on error; caller is responsible to free buffers */
FL bool_t      qp_decode_header(struct str *out, struct str const *in);
FL bool_t      qp_decode_part(struct str *out, struct str const *in,
                  struct str *outrest, struct str *inrest_or_null);

/* How much space is necessary to encode len bytes in Base64, worst case.
 * Includes room for (CR/LF/CRLF and) terminator, UIZ_MAX on overflow */
FL size_t      b64_encode_calc_size(size_t len);

/* Note these simply convert all the input (if possible), including the
 * insertion of NL sequences if B64_CRLF or B64_LF is set (and multiple thereof
 * if B64_MULTILINE is set).
 * Thus, in the B64_BUF case, better call b64_encode_calc_size() first.
 * Return NULL on error (overflow; cannot happen for B64_BUF) */
FL struct str * b64_encode(struct str *out, struct str const *in,
                  enum b64flags flags);
FL struct str * b64_encode_buf(struct str *out, void const *vp, size_t vp_len,
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
FL bool_t      b64_decode(struct str *out, struct str const *in);
FL bool_t      b64_decode_header(struct str *out, struct str const *in);
FL bool_t      b64_decode_part(struct str *out, struct str const *in,
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
 * TODO in fact it should take a HeaderField* and append a HeaderFieldParam*! */
FL si8_t       mime_param_create(struct str *result, char const *name,
                  char const *value);

/* Get the boundary out of a Content-Type: multipart/xyz header field, return
 * autorec_alloc()ed copy of it; store strlen() in *len if set */
FL char *      mime_param_boundary_get(char const *headerbody, size_t *len);

/* Create a autorec_alloc()ed MIME boundary */
FL char *      mime_param_boundary_create(void);

/*
 * mime-parse.c
 */

/* Create MIME part object tree for and of mp */
FL struct mimepart * mime_parse_msg(struct message *mp,
                        enum mime_parse_flags mpf);

/*
 * mime-types.c
 */

/* `(un)?mimetype' commands */
FL int         c_mimetype(void *v);
FL int         c_unmimetype(void *v);

/* Check whether the Content-Type name is internally known */
FL bool_t n_mimetype_check_mtname(char const *name);

/* Return a Content-Type matching the name, or NULL if none could be found */
FL char *n_mimetype_classify_filename(char const *name);

/* Classify content of *fp* as necessary and fill in arguments; **charset* is
 * set to *charset-7bit* or charset_iter_or_fallback() if NULL */
FL enum conversion n_mimetype_classify_file(FILE *fp, char const **contenttype,
                     char const **charset, int *do_iconv);

/* Dependend on *mime-counter-evidence* mpp->m_ct_type_usr_ovwr will be set,
 * but otherwise mpp is const.  for_user_context rather maps 1:1 to
 * MIME_PARSE_FOR_USER_CONTEXT */
FL enum mimecontent n_mimetype_classify_part(struct mimepart *mpp,
                        bool_t for_user_context);

/* Query handler for a part, return the plain type (& MIME_HDL_TYPE_MASK).
 * mhp is anyway initialized (mh_flags, mh_msg) */
FL enum mime_handler_flags n_mimetype_handler(struct mime_handler *mhp,
                              struct mimepart const *mpp,
                              enum sendaction action);

/*
 * nam-a-grp.c
 */

/* Allocate a single element of a name list, initialize its name field to the
 * passed name and return it */
FL struct name * nalloc(char const *str, enum gfield ntype);

/* Alloc an Fcc: entry TODO temporary only i hope */
FL struct name *nalloc_fcc(char const *file);

/* Like nalloc(), but initialize from content of np */
FL struct name * ndup(struct name *np, enum gfield ntype);

/* Concatenate the two passed name lists, return the result */
FL struct name * cat(struct name *n1, struct name *n2);

/* Duplicate np */
FL struct name *n_namelist_dup(struct name const *np, enum gfield ntype);

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

/* Get a lextract() list via n_go_input_cp(), reassigning to *np* */
FL struct name * grab_names(enum n_go_input_flags gif, char const *field,
                     struct name *np, int comma, enum gfield gflags);

/* Check whether n1 n2 share the domain name */
FL bool_t      name_is_same_domain(struct name const *n1,
                  struct name const *n2);

/* Check all addresses in np and delete invalid ones; if set_on_error is not
 * NULL it'll be set to TRU1 for error or -1 for "hard fail" error */
FL struct name * checkaddrs(struct name *np, enum expand_addr_check_mode eacm,
                  si8_t *set_on_error);

/* Vaporise all duplicate addresses in hp (.h_(to|cc|bcc)) so that an address
 * that "first" occurs in To: is solely in there, ditto Cc:, after expanding
 * aliases etc.  eacm and set_on_error are passed to checkaddrs().
 * metoo is implied (for usermap()).
 * After updating hp to the new state this returns a flat list of all
 * addressees, which may be NULL */
FL struct name *n_namelist_vaporise_head(bool_t strip_alternates,
                  struct header *hp, enum expand_addr_check_mode eacm,
                  si8_t *set_on_error);

/* Map all of the aliased users in the invoker's mailrc file and insert them
 * into the list */
FL struct name * usermap(struct name *names, bool_t force_metoo);

/* Remove all of the duplicates from the passed name list by insertion sorting
 * them, then checking for dups.  Return the head of the new list */
FL struct name * elide(struct name *names);

/* `(un)?alternates' deal with the list of alternate names */
FL int c_alternates(void *v);
FL int c_unalternates(void *v);

/* If keep_single is set one alternates member will be allowed in np */
FL struct name *n_alternates_remove(struct name *np, bool_t keep_single);

/* Likewise, is name an alternate in broadest sense? */
FL bool_t n_is_myname(char const *name);

/* `addrcodec' */
FL int c_addrcodec(void *vp);

/* `(un)?commandalias'.
 * And whether a `commandalias' name exists, returning name or NULL, pointing
 * expansion_or_null to expansion if set: both point into internal storage */
FL int c_commandalias(void *vp);
FL int c_uncommandalias(void *vp);

FL char const *n_commandalias_exists(char const *name,
                  struct str const **expansion_or_null);

/* Is name a valid alias */
FL bool_t n_alias_is_valid_name(char const *name);

/* `(un)?alias' */
FL int         c_alias(void *v);
FL int         c_unalias(void *v);

/* `(un)?ml(ist|subscribe)', and a check whether a name is a (wanted) list;
 * give MLIST_OTHER to the latter to search for any, in which case all
 * receivers are searched until EOL or MLIST_SUBSCRIBED is seen */
FL int         c_mlist(void *v);
FL int         c_unmlist(void *v);
FL int         c_mlsubscribe(void *v);
FL int         c_unmlsubscribe(void *v);

FL enum mlist_state is_mlist(char const *name, bool_t subscribed_only);
FL enum mlist_state is_mlist_mp(struct message *mp, enum mlist_state what);

/* `(un)?shortcut', and check if str is one, return expansion or NULL */
FL int         c_shortcut(void *v);
FL int         c_unshortcut(void *v);

FL char const * shortcut_expand(char const *str);

/* `(un)?charsetalias', and try to expand a charset, return mapping or itself.
 * The charset to expand must have gone through iconv_normalize_name() */
FL int c_charsetalias(void *vp);
FL int c_uncharsetalias(void *vp);

FL char const *n_charsetalias_expand(char const *cp);

/* `(un)?filetype', and check whether file has a known (stat(2)ed) "equivalent",
 * as well as check whether (extension of) file is known, respectively;
 * res_or_null can be NULL, otherwise on success result data must be copied */
FL int c_filetype(void *vp);
FL int c_unfiletype(void *vp);

FL bool_t n_filetype_trial(struct n_file_type *res_or_null, char const *file);
FL bool_t n_filetype_exists(struct n_file_type *res_or_null, char const *file);

/*
 * path.c
 */

/* Test to see if the passed file name is a directory, return true if it is.
 * If check_access is set, we also access(2): if it is TRUM1 only X_OK|R_OK is
 * tested, otherwise X_OK|R_OK|W_OK. */
FL bool_t n_is_dir(char const *name, bool_t check_access);

/* Recursively create a directory */
FL bool_t n_path_mkdir(char const *name);

/* Delete a file, but only if the file is a plain file; return FAL0 on system
 * error and TRUM1 if name is not a plain file, return TRU1 on success */
FL bool_t n_path_rm(char const *name);

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
FL int pop3_setfile(char const *who, char const *server, enum fedit_mode fm);

/*  */
FL enum okay   pop3_header(struct message *m);

/*  */
FL enum okay   pop3_body(struct message *m);

/*  */
FL bool_t      pop3_quit(bool_t hold_sigs_on);
#endif /* HAVE_POP3 */

/*
 * popen.c
 * Subprocesses, popen, but also file handling with registering
 */

/* For program startup in main.c: initialize process manager */
FL void        n_child_manager_start(void);

/* xflags may be NULL.  Implied: cloexec */
FL FILE *      safe_fopen(char const *file, char const *oflags, int *xflags);

/* oflags implied: cloexec,OF_REGISTER.
 * Exception is Fdopen() if nocloexec is TRU1, but otherwise even for it the fd
 * creator has to take appropriate steps in order to ensure this is true! */
FL FILE *      Fopen(char const *file, char const *oflags);
FL FILE *      Fdopen(int fd, char const *oflags, bool_t nocloexec);

FL int         Fclose(FILE *fp);

/* TODO: Should be Mailbox::create_from_url(URL::from_string(DATA))!
 * Open file according to oflags (see popen.c).  Handles compressed files,
 * maildir etc.  If ft_or_null is given it will be filled accordingly */
FL FILE * n_fopen_any(char const *file, char const *oflags,
            enum n_fopen_state *fs_or_null);

/* Create a temporary file in *TMPDIR*, use namehint for its name (prefix
 * unless OF_SUFFIX is set, in which case namehint is an extension that MUST be
 * part of the resulting filename, otherwise Ftmp() will fail), store the
 * unique name in fn if set (and unless OF_UNLINK is set in oflags), creating
 * n_alloc() storage or n_autorec_alloc() if OF_NAME_AUTOREC is set,
 * and return a stdio FILE pointer with access oflags.
 * One of OF_WRONLY and OF_RDWR must be set.  Implied: 0600,cloexec */
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
 * called from within the child process.
 * n_psignal() takes a FILE* returned by Popen, and returns <0 if no process
 * can be found, 0 on success, and an errno number on kill(2) failure */
FL FILE *Popen(char const *cmd, char const *mode, char const *shell,
            char const **env_addon, int newfd1);
FL bool_t Pclose(FILE *fp, bool_t dowait);
VL int n_psignal(FILE *fp, int sig);

/* In n_PSO_INTERACTIVE, we want to go over $PAGER.
 * These are specialized version of Popen()/Pclose() which also encapsulate
 * error message printing, terminal handling etc. additionally */
FL FILE *      n_pager_open(void);
FL bool_t      n_pager_close(FILE *fp);

/*  */
FL void        close_all_files(void);

/* Run a command without a shell, with optional arguments and splicing of stdin
 * and stdout.  FDs may also be n_CHILD_FD_NULL and n_CHILD_FD_PASS, meaning
 * to redirect from/to /dev/null or pass through our own set of FDs; in the
 * latter case terminal capabilities are saved/restored if possible.
 * The command name can be a sequence of words.
 * Signals must be handled by the caller.  "Mask" contains the signals to
 * ignore in the new process.  SIGINT is enabled unless it's in the mask.
 * If env_addon_or_null is set, it is expected to be a NULL terminated
 * array of "K=V" strings to be placed into the childs environment.
 * wait_status_or_null is set to waitpid(2) status if given */
FL int n_child_run(char const *cmd, sigset_t *mask, int infd, int outfd,
         char const *a0_or_null, char const *a1_or_null, char const *a2_or_null,
         char const **env_addon_or_null, int *wait_status_or_null);

/* Like n_child_run(), but don't wait for the command to finish (use
 * n_child_wait() for waiting on a successful return value).
 * Also it is usually an error to use n_CHILD_FD_PASS for this one */
FL int n_child_start(char const *cmd, sigset_t *mask, int infd, int outfd,
         char const *a0_or_null, char const *a1_or_null, char const *a2_or_null,
         char const **env_addon_or_null);

/* Fork a child process, enable the other three:
 * - in-child image preparation
 * - mark a child as don't care
 * - wait for child pid, return whether we've had a normal n_EXIT_OK exit.
 *   If wait_status_or_null is set, it is set to the waitpid(2) status */
FL int n_child_fork(void);
FL void n_child_prepare(sigset_t *nset, int infd, int outfd);
FL void n_child_free(int pid);
FL bool_t n_child_wait(int pid, int *wait_status_or_null);

/*
 * quit.c
 */

/* Save all of the undetermined messages at the top of "mbox".  Save all
 * untouched messages back in the system mailbox.  Remove the system mailbox,
 * if none saved there.
 * TODO v15 Note: assumes hold_sigs() has been called _and_ can be temporarily
 * TODO dropped via a single rele_sigs() if hold_sigs_on */
FL bool_t      quit(bool_t hold_sigs_on);

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
                  char const *prefix, enum sendaction action, ui64_t *stats);

/*
 * sendout.c
 */

/* Interface between the argument list and the mail1 routine which does all the
 * dirty work */
FL int n_mail(enum n_mailsend_flags msf, struct name *to, struct name *cc,
         struct name *bcc, char const *subject, struct attachment *attach,
         char const *quotefile);

/* `mail' and `Mail' commands, respectively */
FL int c_sendmail(void *v);
FL int c_Sendmail(void *v);

/* Mail a message on standard input to the people indicated in the passed
 * header.  (Internal interface) */
FL enum okay n_mail1(enum n_mailsend_flags flags, struct header *hp,
               struct message *quote, char const *quotefile);

/* Create a Date: header field.
 * We compare the localtime() and gmtime() results to get the timezone, because
 * numeric timezones are easier to read and because $TZ isn't always set */
FL int         mkdate(FILE *fo, char const *field);

/* Dump the to, subject, cc header on the passed file buffer.
 * nosend_msg tells us not to dig to deep but to instead go for compose mode or
 * editing a message (yet we're stupid and cannot do it any better) - if it is
 * TRUM1 then we're really in compose mode and will produce some fields for
 * easier filling in (see n_run_editor() proto for this hack) */
FL bool_t n_puthead(bool_t nosend_msg, struct header *hp, FILE *fo,
                  enum gfield w, enum sendaction action,
                  enum conversion convert, char const *contenttype,
                  char const *charset);

/*  */
FL enum okay   resend_msg(struct message *mp, struct header *hp,
                  bool_t add_resent);

/* $DEAD */
FL void        savedeadletter(FILE *fp, bool_t fflush_rewind_first);

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
FL char *fexpand(char const *name, enum fexp_mode fexpm);

/* Parse the next shell token from input (->s and ->l are adjusted to the
 * remains, data is constant beside that; ->s may be NULL if ->l is 0, if ->l
 * EQ UIZ_MAX strlen(->s) is used) and append the resulting output to store.
 * If cookie is not NULL and we're in double-quotes then ${@} will be exploded
 * just as known from the sh(1)ell in that case */
FL enum n_shexp_state n_shexp_parse_token(enum n_shexp_parse_flags flags,
                        struct n_string *store, struct str *input,
                        void const **cookie);

/* Quick+dirty simplified : if an error occurs, returns a copy of *cp and set
 * *cp to NULL, otherwise advances *cp to over the parsed token */
FL char *n_shexp_parse_token_cp(enum n_shexp_parse_flags flags,
            char const **cp);

/* Quote input in a way that can, in theory, be fed into parse_token() again.
 * ->s may be NULL if ->l is 0, if ->l EQ UIZ_MAX strlen(->s) is used.
 * If rndtrip is true we try to make the resulting string "portable" (by
 * converting Unicode to \u etc.), otherwise we produce something to be
 * consumed "now", i.e., to display for the user.
 * Resulting output is _appended_ to store.
 * TODO Note: last resort, since \u and $ expansions etc. are necessarily
 * TODO already expanded and can thus not be reverted, but ALL we have */
FL struct n_string *n_shexp_quote(struct n_string *store,
                     struct str const *input, bool_t rndtrip);
FL char *n_shexp_quote_cp(char const *cp, bool_t rndtrip);

/* Can name be used as a variable name?  I.e., this returns false for special
 * parameter names like $# etc. */
FL bool_t n_shexp_is_valid_varname(char const *name);

/* `shcodec' */
FL int c_shcodec(void *vp);

/*
 * signal.c
 */

#ifdef HAVE_DEVEL
FL int         c_sigstate(void *);
#endif

FL void        n_raise(int signo);

/* Provide BSD-like signal() on all systems TODO v15 -> SysV -> n_signal() */
FL sighandler_type safe_signal(int signum, sighandler_type handler);

/* Provide reproducable non-restartable signal handler installation */
FL n_sighdl_t  n_signal(int signo, n_sighdl_t hdl);

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
# define NYD_IN                  _nyd_chirp(1, __FILE__, __LINE__, __FUN__)
# define NYD_OU                  _nyd_chirp(2, __FILE__, __LINE__, __FUN__)
# define NYD                     _nyd_chirp(0, __FILE__, __LINE__, __FUN__)
# define NYD_X                   _nyd_chirp(0, __FILE__, __LINE__, __FUN__)
# ifdef HAVE_NYD2
#  define NYD2_IN                _nyd_chirp(1, __FILE__, __LINE__, __FUN__)
#  define NYD2_OU                _nyd_chirp(2, __FILE__, __LINE__, __FUN__)
#  define NYD2                   _nyd_chirp(0, __FILE__, __LINE__, __FUN__)
# endif
#else
# undef HAVE_NYD
#endif
#ifndef NYD
# define NYD_IN                  do {} while (0)
# define NYD_OU                  do {} while (0)
# define NYD                     do {} while (0)
# define NYD_X                   do {} while (0) /* XXX LEGACY */
#endif
#ifndef NYD2
# define NYD2_IN                 do {} while (0)
# define NYD2_OU                 do {} while (0)
# define NYD2                    do {} while (0)
#endif
#define NYD_ENTER NYD_IN /* TODO obsolete _ENTER and _LEAVE */
#define NYD_LEAVE NYD_OU
#define NYD2_ENTER NYD2_IN
#define NYD2_LEAVE NYD2_OU


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
/* Immediately closes the socket for CPROTO_CERTINFO */
FL bool_t      sopen(struct sock *sp, struct url *urlp);
FL int         sclose(struct sock *sp);
FL enum okay   swrite(struct sock *sp, char const *data);
FL enum okay   swrite1(struct sock *sp, char const *data, int sz,
                  int use_buffer);

/*  */
FL int         sgetline(char **line, size_t *linesize, size_t *linelen,
                  struct sock *sp n_MEMORY_DEBUG_ARGS);
# ifdef HAVE_MEMORY_DEBUG
#  define sgetline(A,B,C,D)      sgetline(A, B, C, D, __FILE__, __LINE__)
# endif
#endif

/*
 * spam.c
 */

#ifdef HAVE_SPAM
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
FL char *      savestr(char const *str n_MEMORY_DEBUG_ARGS);
FL char *      savestrbuf(char const *sbuf, size_t slen n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define savestr(CP)             savestr(CP, __FILE__, __LINE__)
# define savestrbuf(CBP,CBL)     savestrbuf(CBP, CBL, __FILE__, __LINE__)
#endif

/* Concatenate cp2 onto cp1 (if not NULL), separated by sep (if not '\0') */
FL char *      savecatsep(char const *cp1, char sep, char const *cp2
                  n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define savecatsep(S1,SEP,S2)   savecatsep(S1, SEP, S2, __FILE__, __LINE__)
#endif

/* Make copy of argument incorporating old one, if set, separated by space */
#define save2str(S,O)            savecatsep(O, ' ', S)

/* strcat */
#define savecat(S1,S2)           savecatsep(S1, '\0', S2)

/*  */
FL struct str * str_concat_csvl(struct str *self, ...);

/*  */
FL struct str * str_concat_cpa(struct str *self, char const * const *cpa,
                  char const *sep_o_null n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define str_concat_cpa(S,A,N)   str_concat_cpa(S, A, N, __FILE__, __LINE__)
#endif

/* Plain char* support, not auto-reclaimed (unless noted) */

/* Are any of the characters in template contained in dat? */
FL bool_t n_anyof_buf(char const *template, char const *dat, size_t len);
#define n_anyof_cp(S1,S2) n_anyof_buf(S1, S2, UIZ_MAX)

/* Treat *iolist as a sep separated list of strings; find and return the
 * next entry, trimming surrounding whitespace, and point *iolist to the next
 * entry or to NULL if no more entries are contained.  If ignore_empty is
 * set empty entries are started over.
 * strsep_esc() is identical but allows reverse solidus escaping of sep, too.
 * Return NULL or an entry */
FL char *n_strsep(char **iolist, char sep, bool_t ignore_empty);
FL char *n_strsep_esc(char **iolist, char sep, bool_t ignore_empty);

/* Is *as1* a valid prefix of *as2*? */
FL bool_t is_prefix(char const *as1, char const *as2);

/* Reverse solidus quote (" and \) v'alue, and return autorec_alloc()ed */
FL char *      string_quote(char const *v);

/* Convert a string to lowercase, in-place and with multibyte-aware */
FL void        makelow(char *cp);

/* Is *sub* a substring of *str*, case-insensitive and multibyte-aware? */
FL bool_t      substr(char const *str, char const *sub);

/*  */
FL char *      sstpcpy(char *dst, char const *src);
FL char *      sstrdup(char const *cp n_MEMORY_DEBUG_ARGS);
FL char *      sbufdup(char const *cp, size_t len n_MEMORY_DEBUG_ARGS);
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

/* Case-independent ASCII check whether as1 is the initial substring of as2 */
FL bool_t      is_asccaseprefix(char const *as1, char const *as2);
FL bool_t      is_ascncaseprefix(char const *as1, char const *as2, size_t sz);

/* struct str related support funs TODO _cp->_cs! */

/* *self->s* is n_realloc()ed */
#define n_str_dup(S, T)          n_str_assign_buf((S), (T)->s, (T)->l)

/* *self->s* is n_realloc()ed; if buflen==UIZ_MAX strlen() is called unless
 * buf is NULL; buf may be NULL if buflen is 0 */
FL struct str * n_str_assign_buf(struct str *self,
                  char const *buf, uiz_t buflen n_MEMORY_DEBUG_ARGS);
#define n_str_assign(S, T)       n_str_assign_buf(S, (T)->s, (T)->l)
#define n_str_assign_cp(S, CP)   n_str_assign_buf(S, CP, UIZ_MAX)

/* *self->s* is n_realloc()ed, *self->l* incremented; if buflen==UIZ_MAX
 * strlen() is called unless buf is NULL; buf may be NULL if buflen is 0 */
FL struct str * n_str_add_buf(struct str *self, char const *buf, uiz_t buflen
                  n_MEMORY_DEBUG_ARGS);
#define n_str_add(S, T)          n_str_add_buf(S, (T)->s, (T)->l)
#define n_str_add_cp(S, CP)      n_str_add_buf(S, CP, UIZ_MAX)

#ifdef HAVE_MEMORY_DEBUG
# define n_str_assign_buf(S,B,BL) n_str_assign_buf(S, B, BL, __FILE__, __LINE__)
# define n_str_add_buf(S,B,BL)   n_str_add_buf(S, B, BL, __FILE__, __LINE__)
#endif

/* Remove leading and trailing spacechar()s and *ifs-ws*, respectively.
 * The ->s and ->l of the string will be adjusted, but no NUL termination will
 * be applied to a possibly adjusted buffer!
 * If dofaults is set, " \t\n" is always trimmed (in addition) */
FL struct str *n_str_trim(struct str *self, enum n_str_trim_flags stf);
FL struct str *n_str_trim_ifs(struct str *self, bool_t dodefaults);

/* struct n_string
 * May have NULL buffer, may contain embedded NULs */

/* Lifetime.  n_string_gut() is optional for _creat_auto() strings */
#define n_string_creat(S) \
   ((S)->s_dat = NULL, (S)->s_len = (S)->s_auto = (S)->s_size = 0, (S))
#define n_string_creat_auto(S) \
   ((S)->s_dat = NULL, (S)->s_len = (S)->s_size = 0, (S)->s_auto = TRU1, (S))
#define n_string_gut(S) \
      ((S)->s_dat != NULL ? (void)n_string_clear(S) : (void)0)

/* Truncate to size, which must be LE current length */
#define n_string_trunc(S,L) \
   (assert(UICMP(z, L, <=, (S)->s_len)), (S)->s_len = (ui32_t)(L), (S))

/* Take/Release buffer ownership */
#define n_string_take_ownership(SP,B,S,L) \
   (assert((SP)->s_dat == NULL), assert((S) == 0 || (B) != NULL),\
    assert((L) < (S) || (L) == 0),\
    (SP)->s_dat = (B), (SP)->s_len = (L), (SP)->s_size = (S), (SP))
#define n_string_drop_ownership(SP) \
   ((SP)->s_dat = NULL, (SP)->s_len = (SP)->s_size = 0, (SP))

/* Release all memory */
FL struct n_string *n_string_clear(struct n_string *self n_MEMORY_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
# define n_string_clear(S) \
   ((S)->s_size != 0 ? (n_string_clear)(S, __FILE__, __LINE__) : (S))
#else
# define n_string_clear(S)       ((S)->s_size != 0 ? (n_string_clear)(S) : (S))
#endif

/* Check whether a buffer of Len bytes can be inserted into S(elf) */
#define n_string_get_can_book(L) ((uiz_t)SI32_MAX - n_ALIGN(1) > L)
#define n_string_can_book(S,L) \
   (n_string_get_can_book(L) &&\
    (uiz_t)SI32_MAX - n_ALIGN(1) - (L) > (S)->s_len)

/* Reserve room for noof additional bytes, but don't adjust length (yet) */
FL struct n_string *n_string_reserve(struct n_string *self, size_t noof
                     n_MEMORY_DEBUG_ARGS);
#define n_string_book n_string_reserve

/* Resize to exactly nlen bytes; any new storage isn't initialized */
FL struct n_string *n_string_resize(struct n_string *self, size_t nlen
                     n_MEMORY_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
# define n_string_reserve(S,N)   (n_string_reserve)(S, N, __FILE__, __LINE__)
# define n_string_resize(S,N)    (n_string_resize)(S, N, __FILE__, __LINE__)
#endif

/* */
FL struct n_string *n_string_push_buf(struct n_string *self, char const *buf,
                     size_t buflen n_MEMORY_DEBUG_ARGS);
#define n_string_push(S, T)       n_string_push_buf(S, (T)->s_len, (T)->s_dat)
#define n_string_push_cp(S,CP)    n_string_push_buf(S, CP, UIZ_MAX)
FL struct n_string *n_string_push_c(struct n_string *self, char c
                     n_MEMORY_DEBUG_ARGS);

#define n_string_assign(S,T)     ((S)->s_len = 0, n_string_push(S, T))
#define n_string_assign_c(S,C)   ((S)->s_len = 0, n_string_push_c(S, C))
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
                     size_t buflen n_MEMORY_DEBUG_ARGS);
#define n_string_unshift(S,T) \
   n_string_unshift_buf(S, (T)->s_len, (T)->s_dat)
#define n_string_unshift_cp(S,CP) \
      n_string_unshift_buf(S, CP, UIZ_MAX)
FL struct n_string *n_string_unshift_c(struct n_string *self, char c
                     n_MEMORY_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
# define n_string_unshift_buf(S,B,BL) \
   n_string_unshift_buf(S,B,BL, __FILE__, __LINE__)
# define n_string_unshift_c(S,C) n_string_unshift_c(S, C, __FILE__, __LINE__)
#endif

/* */
FL struct n_string *n_string_insert_buf(struct n_string *self, size_t idx,
                     char const *buf, size_t buflen n_MEMORY_DEBUG_ARGS);
#define n_string_insert(S,I,T) \
   n_string_insert_buf(S, I, (T)->s_len, (T)->s_dat)
#define n_string_insert_cp(S,I,CP) \
      n_string_insert_buf(S, I, CP, UIZ_MAX)
FL struct n_string *n_string_insert_c(struct n_string *self, size_t idx,
                     char c n_MEMORY_DEBUG_ARGS);

#ifdef HAVE_MEMORY_DEBUG
# define n_string_insert_buf(S,I,B,BL) \
   n_string_insert_buf(S, I, B, BL, __FILE__, __LINE__)
# define n_string_insert_c(S,I,C) n_string_insert_c(S, I, C, __FILE__, __LINE__)
#endif

/* */
FL struct n_string *n_string_cut(struct n_string *self, size_t idx, size_t len);

/* Ensure self has a - NUL terminated - buffer, and return that.
 * The latter may return the pointer to an internal empty RODATA instead */
FL char *      n_string_cp(struct n_string *self n_MEMORY_DEBUG_ARGS);
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

/* UTF-8 / UTF-32 stuff */

/* ..and update arguments to point after range; returns UI32_MAX on error, in
 * which case the arguments will have been stepped one byte */
FL ui32_t      n_utf8_to_utf32(char const **bdat, size_t *blen);

/* buf must be large enough also for NUL, it's new length will be returned */
FL size_t      n_utf32_to_utf8(ui32_t c, char *buf);

/* Our iconv(3) wrappers */

/* Returns a newly n_autorec_alloc()ated thing if there were adjustments.
 * Return value is always smaller or of equal size.
 * NULL will be returned if cset is an invalid character set name */
FL char *n_iconv_normalize_name(char const *cset);

/* Is it ASCII indeed? */
FL bool_t n_iconv_name_is_ascii(char const *cset);

#ifdef HAVE_ICONV
FL iconv_t     n_iconv_open(char const *tocode, char const *fromcode);
/* If *cd* == *iconvd*, assigns -1 to the latter */
FL void        n_iconv_close(iconv_t cd);

/* Reset encoding state */
FL void        n_iconv_reset(iconv_t cd);

/* iconv(3), but return n_err_no or 0 upon success.
 * The err_no may be ERR_NOENT unless n_ICONV_IGN_NOREVERSE is set in icf.
 * iconv_str() auto-grows on ERR_2BIG errors; in and in_rest_or_null may be
 * the same object.
 * Note: ERR_INVAL (incomplete sequence at end of input) is NOT handled, so the
 * replacement character must be added manually if that happens at EOF!
 * TODO These must be contexts.  For now we duplicate n_err_no into
 * TODO n_iconv_err_no in order to be able to access it when stuff happens
 * TODO "in between"! */
FL int         n_iconv_buf(iconv_t cd, enum n_iconv_flags icf,
                  char const **inb, size_t *inbleft,
                  char **outb, size_t *outbleft);
FL int         n_iconv_str(iconv_t icp, enum n_iconv_flags icf,
                  struct str *out, struct str const *in,
                  struct str *in_rest_or_null);

/* If tocode==NULL, uses *ttycharset*.  If fromcode==NULL, uses UTF-8.
 * Returns a autorec_alloc()ed buffer or NULL */
FL char *      n_iconv_onetime_cp(enum n_iconv_flags icf,
                  char const *tocode, char const *fromcode, char const *input);
#endif

/*
 * termcap.c
 * This is a little bit hairy since it provides some stuff even if HAVE_TERMCAP
 * is false due to encapsulation desire
 */

#ifdef n_HAVE_TCAP
/* termcap(3) / xy lifetime handling -- only called if we're n_PSO_INTERACTIVE
 * but not doing something in n_PO_QUICKRUN_MASK */
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

#  define n_TERMCAP_RESUME(CPL)  do{ n_termcap_resume(CPL); }while(0)
#  define n_TERMCAP_SUSPEND(CPL) do{ n_termcap_suspend(CPL); }while(0)
# endif

/* Command multiplexer, returns FAL0 on I/O error, TRU1 on success and TRUM1
 * for commands which are not available and have no built-in fallback.
 * For query options the return represents a true value and -1 error.
 * Will return FAL0 directly unless we've been initialized.
 * By convention unused argument slots are given as -1 */
FL ssize_t     n_termcap_cmd(enum n_termcap_cmd cmd, ssize_t a1, ssize_t a2);
# define n_termcap_cmdx(CMD)     n_termcap_cmd(CMD, -1, -1)

/* Query multiplexer.  If query is n__TERMCAP_QUERY_MAX1 then
 * tvp->tv_data.tvd_string must contain the name of the query to look up; this
 * is used to lookup just about *any* (string) capability.
 * Returns TRU1 on success and TRUM1 for queries for which a built-in default
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

#ifndef n_TERMCAP_RESUME
# define n_TERMCAP_RESUME(CPL) do{;}while(0)
# define n_TERMCAP_SUSPEND(CPL) do{;}while(0);
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

#ifdef HAVE_TLS
/*  */
FL void n_tls_set_verify_level(struct url const *urlp);

/* */
FL bool_t n_tls_verify_decide(void);

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

/* `certsave' */
FL int c_certsave(void *vp);

/* */
FL bool_t n_tls_rfc2595_hostname_match(char const *host, char const *pattern);

/* `tls' */
FL int c_tls(void *vp);
#endif /* HAVE_TLS */

/*
 * tty.c
 */

/* Return whether user says yes, on STDIN if interactive.
 * Uses noninteract_default, the return value for non-interactive use cases,
 * as a hint for n_boolify() and chooses the yes/no string to append to prompt
 * accordingly.  If prompt is NULL "Continue" is used instead.
 * Handles+reraises SIGINT */
FL bool_t getapproval(char const *prompt, bool_t noninteract_default);

#ifdef HAVE_SOCKETS
/* Get a password the expected way, return termios_state.ts_linebuf on
 * success or NULL on error */
FL char *getuser(char const *query);

/* Get a password the expected way, return termios_state.ts_linebuf on
 * success or NULL on error.  SIGINT is temporarily blocked, *not* reraised.
 * termios_state_reset() (def.h) must be called anyway */
FL char *getpassword(char const *query);
#endif

/* Create the prompt and return its visual width in columns, which may be 0
 * if evaluation is disabled etc.  The data is placed in store.
 * xprompt is inspected only if prompt is enabled and no *prompt* evaluation
 * takes place */
FL ui32_t n_tty_create_prompt(struct n_string *store, char const *xprompt,
            enum n_go_input_flags gif);

/* Overall interactive terminal life cycle for command line editor library */
#ifdef HAVE_MLE
FL void n_tty_init(void);
FL void n_tty_destroy(bool_t xit_fastpath);
#else
# define n_tty_init() do{;}while(0)
# define n_tty_destroy(B) do{;}while(0)
#endif

/* Read a line after printing prompt (if set and non-empty).
 * If n>0 assumes that *linebuf has n bytes of default content.
 * histok_or_null like for n_go_input().
 * Only the _CTX_ bits in lif are used */
FL int n_tty_readline(enum n_go_input_flags gif, char const *prompt,
         char **linebuf, size_t *linesize, size_t n, bool_t *histok_or_null
         n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define n_tty_readline(A,B,C,D,E,F) \
   (n_tty_readline)(A,B,C,D,E,F,__FILE__,__LINE__)
#endif

/* Add a line (most likely as returned by n_tty_readline()) to the history.
 * Whether and how an entry is added for real depends on gif, e.g.,
 * n_GO_INPUT_HIST_GABBY / *history-gabby* relation.
 * Empty strings are never stored */
FL void n_tty_addhist(char const *s, enum n_go_input_flags gif);

#ifdef HAVE_HISTORY
FL int c_history(void *v);
#endif

/* `bind' and `unbind' */
#ifdef HAVE_KEY_BINDINGS
FL int c_bind(void *v);
FL int c_unbind(void *v);
#endif

/*
 * ui-str.c
 */

/* Parse (onechar of) a given buffer, and generate infos along the way.
 * If _WOUT_CREATE is set in vif, .vic_woudat will be NUL terminated!
 * vicp must be zeroed before first use */
FL bool_t      n_visual_info(struct n_visual_info_ctx *vicp,
                  enum n_visual_info_flags vif);

/* Check (multibyte-safe) how many bytes of buf (which is blen byts) can be
 * safely placed in a buffer (field width) of maxlen bytes */
FL size_t      field_detect_clip(size_t maxlen, char const *buf, size_t blen);

/* Place cp in a autorec_alloc()ed buffer, column-aligned.
 * For header display only */
FL char *      colalign(char const *cp, int col, int fill,
                  int *cols_decr_used_or_null);

/* Convert a string to a displayable one;
 * prstr() returns the result savestr()d, prout() writes it */
FL void        makeprint(struct str const *in, struct str *out);
FL size_t      delctrl(char *cp, size_t len);
FL char *      prstr(char const *s);
FL int         prout(char const *s, size_t sz, FILE *fp);

/* Check whether bidirectional info maybe needed for blen bytes of bdat */
FL bool_t      bidi_info_needed(char const *bdat, size_t blen);

/* Create bidirectional text encapsulation information; without HAVE_NATCH_CHAR
 * the strings are always empty */
FL void        bidi_info_create(struct bidi_info *bip);

/*
 * urlcrecry.c
 */

/* URL en- and decoding according to (enough of) RFC 3986 (RFC 1738).
 * These return a newly autorec_alloc()ated result, or NULL on length excess */
FL char *      urlxenc(char const *cp, bool_t ispath n_MEMORY_DEBUG_ARGS);
FL char *      urlxdec(char const *cp n_MEMORY_DEBUG_ARGS);
#ifdef HAVE_MEMORY_DEBUG
# define urlxenc(CP,P)           urlxenc(CP, P, __FILE__, __LINE__)
# define urlxdec(CP)             urlxdec(CP, __FILE__, __LINE__)
#endif

/* `urlcodec' */
FL int c_urlcodec(void *vp);

FL int c_urlencode(void *v); /* TODO obsolete*/
FL int c_urldecode(void *v); /* TODO obsolete */

/* Parse a RFC 6058 'mailto' URI to a single to: (TODO yes, for now hacky).
 * Return NULL or something that can be converted to a struct name */
FL char *      url_mailto_to_address(char const *mailtop);

/* Return port for proto (and set irv_or_null), or NULL if unknown.
 * For file:// this returns an empty string */
FL char const *n_servbyname(char const *proto, ui16_t *irv_or_null);

#ifdef HAVE_SOCKETS
/* Parse data, which must meet the criteria of the protocol cproto, and fill
 * in the URL structure urlp (URL rather according to RFC 3986) */
FL bool_t      url_parse(struct url *urlp, enum cproto cproto,
                  char const *data);

/* Zero ccp and lookup credentials for communicating with urlp.
 * Return whether credentials are available and valid (for chosen auth) */
FL bool_t      ccred_lookup(struct ccred *ccp, struct url *urlp);
FL bool_t      ccred_lookup_old(struct ccred *ccp, enum cproto cproto,
                  char const *addr);
#endif /* HAVE_SOCKETS */

/* `netrc' */
#ifdef HAVE_NETRC
FL int c_netrc(void *v);
#endif

/* MD5 (RFC 1321) related facilities */
#ifdef HAVE_MD5
# ifdef HAVE_XTLS_MD5
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

/* CRAM-MD5 encode the *user* / *pass* / *b64* combo; NULL on overflow error */
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

/*
 * xtls.c
 */

#ifdef HAVE_XTLS
/* Our wrapper for RAND_bytes(3) */
# if HAVE_RANDOM == n_RANDOM_IMPL_TLS
FL void n_tls_rand_bytes(void *buf, size_t blen);
# endif

/* Will fill in a non-NULL *urlp->url_cert_fprint with auto-reclaimed
 * buffer on success, otherwise urlp is constant */
FL bool_t n_tls_open(struct url *urlp, struct sock *sp);

/*  */
FL void        ssl_gen_err(char const *fmt, ...);

/*  */
FL int         c_verify(void *vp);

/*  */
FL FILE *      smime_sign(FILE *ip, char const *addr);

/*  */
FL FILE *      smime_encrypt(FILE *ip, char const *certfile, char const *to);

FL struct message * smime_decrypt(struct message *m, char const *to,
                     char const *cc, bool_t is_a_verify_call);

/*  */
FL enum okay   smime_certsave(struct message *m, int n, FILE *op);

#endif /* HAVE_XTLS */

/*
 * obs-imap.c
 */

#ifdef HAVE_IMAP
FL void n_go_onintr_for_imap(void);

/* The former returns the input again if no conversion is necessary */
FL char const *imap_path_encode(char const *path, bool_t *err_or_null);
FL char *imap_path_decode(char const *path, bool_t *err_or_null);

FL char const * imap_fileof(char const *xcp);
FL enum okay   imap_noop(void);
FL enum okay   imap_select(struct mailbox *mp, off_t *size, int *count,
                  const char *mbx, enum fedit_mode fm);
FL int imap_setfile(char const *who, const char *xserver, enum fedit_mode fm);
FL enum okay   imap_header(struct message *m);
FL enum okay   imap_body(struct message *m);
FL void        imap_getheaders(int bot, int top);
FL bool_t      imap_quit(bool_t hold_sigs_on);
FL enum okay   imap_undelete(struct message *m, int n);
FL enum okay   imap_unread(struct message *m, int n);
FL int         c_imapcodec(void *vp);
FL int         c_imap_imap(void *vp);
FL int         imap_newmail(int nmail);
FL enum okay   imap_append(const char *xserver, FILE *fp, long offset);
FL int         imap_folders(const char *name, int strip);
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

/* Extract the protocol base and return a duplicate */
FL char *      protbase(char const *cp n_MEMORY_DEBUG_ARGS);
# ifdef HAVE_MEMORY_DEBUG
#  define protbase(CP)           protbase(CP, __FILE__, __LINE__)
# endif
#endif /* HAVE_IMAP */

/*
 * obs-imap-cache.c
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
FL enum okay   cache_setptr(enum fedit_mode fm, int transparent);
FL enum okay   cache_list(struct mailbox *mp, char const *base, int strip,
                  FILE *fp);
FL enum okay   cache_remove(char const *name);
FL enum okay   cache_rename(char const *old, char const *new);
FL ui64_t cached_uidvalidity(struct mailbox *mp);
FL FILE *      cache_queue(struct mailbox *mp);
FL enum okay   cache_dequeue(struct mailbox *mp);
#endif /* HAVE_IMAP */

/*
 * obs-lzw.c
 */
#ifdef HAVE_IMAP
FL int         zwrite(void *cookie, const char *wbp, int num);
FL int         zfree(void *cookie);
FL int         zread(void *cookie, char *rbp, int num);
FL void *      zalloc(FILE *fp);
#endif /* HAVE_IMAP */

#ifndef HAVE_AMALGAMATION
# undef FL
# define FL
#endif

/* s-it-mode */
