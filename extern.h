/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Exported function prototypes.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

FL int      callhook(char const *name, int newmail);

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
#ifdef HAVE_DEBUG
FL void        warn(char const *format, ...);
#endif

/* Provide BSD-like signal() on all (POSIX) systems */
FL sighandler_type safe_signal(int signum, sighandler_type handler);

/* Hold *all* signals but SIGCHLD, and release that total block again */
FL void        hold_all_sigs(void);
FL void        rele_all_sigs(void);

/* Hold HUP/QUIT/INT */
FL void        hold_sigs(void);
FL void        rele_sigs(void);

FL void        touch(struct message *mp);
FL int         is_dir(char const *name);
FL int         argcount(char **argv);
FL char *      colalign(const char *cp, int col, int fill,
                  int *cols_decr_used_or_null);

/* Get our PAGER */
FL char const *get_pager(void);

/* Check wether using a pager is possible/makes sense and is desired by user
 * (*crt* set); return number of screen lines (or *crt*) if so, 0 otherwise */
FL size_t      paging_seems_sensible(void);

/* Use a pager or STDOUT to print *fp*; if *lines* is 0, they'll be counted */
FL void        page_or_print(FILE *fp, size_t lines);
#define try_pager(FP)            page_or_print(FP, 0) /* TODO obsolete */

FL enum protocol  which_protocol(const char *name);

/* Hash the passed string -- uses Chris Torek's hash algorithm */
FL ui32_t      torek_hash(char const *name);
#define hash(S)                  (torek_hash(S) % HSHSIZE) /* xxx COMPAT (?) */

FL unsigned    pjw(const char *cp);
FL long        nextprime(long n);

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

#ifdef HAVE_MD5
/* Store the MD5 checksum as a hexadecimal string in *hex*, *not* terminated */
# define MD5TOHEX_SIZE           32
FL char *      md5tohex(char hex[MD5TOHEX_SIZE], void const *vp);

/* CRAM-MD5 encode the *user* / *pass* / *b64* combo */
FL char *      cram_md5_string(char const *user, char const *pass,
                  char const *b64);
#endif

FL enum okay   makedir(const char *name);
FL enum okay   cwget(struct cw *cw);
FL enum okay   cwret(struct cw *cw);
FL void        cwrelse(struct cw *cw);
FL void        makeprint(struct str const *in, struct str *out);
FL char *      prstr(const char *s);
FL int         prout(const char *s, size_t sz, FILE *fp);

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
/* The *smemtrace* command */
FL int         smemtrace(void *v);
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

/* cache.c */
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
FL enum okay   cache_list(struct mailbox *mp, const char *base, int strip,
                  FILE *fp);
FL enum okay   cache_remove(const char *name);
FL enum okay   cache_rename(const char *old, const char *new);
FL unsigned long cached_uidvalidity(struct mailbox *mp);
FL FILE *      cache_queue(struct mailbox *mp);
FL enum okay   cache_dequeue(struct mailbox *mp);
#endif /* HAVE_IMAP */

/* cmd1.c */
FL int         ccmdnotsupp(void *v);
FL int         headers(void *v);
FL int         scroll(void *v);
FL int         Scroll(void *v);
FL int         screensize(void);
FL int         from(void *v);

/* Print all message in between bottom and topx (including bottom) */
FL void        print_headers(size_t bottom, size_t topx);

FL int         pdot(void *v);
FL int         more(void *v);
FL int         More(void *v);
FL int         type(void *v);
FL int         Type(void *v);
FL int         show(void *v);
FL int         pipecmd(void *v);
FL int         Pipecmd(void *v);
FL int         top(void *v);
FL int         stouch(void *v);
FL int         mboxit(void *v);
FL int         folders(void *v);

/* cmd2.c */
FL int         next(void *v);
FL int         save(void *v);
FL int         Save(void *v);
FL int         copycmd(void *v);
FL int         Copycmd(void *v);
FL int         cmove(void *v);
FL int         cMove(void *v);
FL int         cdecrypt(void *v);
FL int         cDecrypt(void *v);
#ifdef HAVE_DEBUG
FL int         clobber(void *v);
FL int         core(void *v);
#endif
FL int         cwrite(void *v);
FL int         delete(void *v);
FL int         deltype(void *v);
FL int         undeletecmd(void *v);
FL int         retfield(void *v);
FL int         igfield(void *v);
FL int         saveretfield(void *v);
FL int         saveigfield(void *v);
FL int         fwdretfield(void *v);
FL int         fwdigfield(void *v);
FL int         unignore(void *v);
FL int         unretain(void *v);
FL int         unsaveignore(void *v);
FL int         unsaveretain(void *v);
FL int         unfwdignore(void *v);
FL int         unfwdretain(void *v);

/* cmd3.c */
FL int         shell(void *v);
FL int         dosh(void *v);
FL int         help(void *v);

/* Print user's working directory */
FL int         c_cwd(void *v);

/* Change user's working directory */
FL int         c_chdir(void *v);

FL int         respond(void *v);
FL int         respondall(void *v);
FL int         respondsender(void *v);
FL int         followup(void *v);
FL int         followupall(void *v);
FL int         followupsender(void *v);
FL int         preserve(void *v);
FL int         unread(void *v);
FL int         seen(void *v);
FL int         messize(void *v);
FL int         rexit(void *v);
/* Set or display a variable value.  Syntax is similar to that of sh */
FL int         set(void *v);
FL int         unset(void *v);
FL int         group(void *v);
FL int         ungroup(void *v);
FL int         cfile(void *v);
FL int         echo(void *v);
FL int         Respond(void *v);
FL int         Followup(void *v);
FL int         forwardcmd(void *v);
FL int         Forwardcmd(void *v);

/* if.else.endif conditional execution */
FL int         c_if(void *v);
FL int         c_else(void *v);
FL int         c_endif(void *v);

FL int         alternates(void *v);
FL int         resendcmd(void *v);
FL int         Resendcmd(void *v);
FL int         newmail(void *v);
FL int         shortcut(void *v);
FL struct shortcut *get_shortcut(const char *str);
FL int         unshortcut(void *v);
FL int         cflag(void *v);
FL int         cunflag(void *v);
FL int         canswered(void *v);
FL int         cunanswered(void *v);
FL int         cdraft(void *v);
FL int         cundraft(void *v);
FL int         cnoop(void *v);
FL int         cremove(void *v);
FL int         crename(void *v);

/* collect.c */

FL FILE *      collect(struct header *hp, int printheaders, struct message *mp,
                  char *quotefile, int doprefix);

FL void        savedeadletter(FILE *fp, int fflush_rewind_first);

/* dotlock.c */
FL int         fcntl_lock(int fd, int type);
FL int         dot_lock(const char *fname, int fd, int pollinterval, FILE *fp,
                  const char *msg);
FL void        dot_unlock(const char *fname);

/* edit.c */
FL int         editor(void *v);
FL int         visual(void *v);
FL FILE *      run_editor(FILE *fp, off_t size, int type, int readonly,
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
FL ssize_t     quoteflt_push(struct quoteflt *self,
                  char const *dat, size_t len);
FL ssize_t     quoteflt_flush(struct quoteflt *self);

/* fio.c */

/* fgets() replacement to handle lines of arbitrary size and with embedded \0
 * characters.
 * *line* - line buffer. *line be NULL.
 * *linesize* - allocated size of line buffer.
 * *count* - maximum characters to read. May be NULL.
 * *llen* - length_of_line(*line).
 * *fp* - input FILE.
 * *appendnl* - always terminate line with \n, append if necessary.
 */
FL char *      fgetline(char **line, size_t *linesize, size_t *count,
                  size_t *llen, FILE *fp, int appendnl SMALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define fgetline(A,B,C,D,E,F)   \
   fgetline(A, B, C, D, E, F, __FILE__, __LINE__)
#endif

/* Read up a line from the specified input into the linebuffer.
 * Return the number of characters read.  Do not include the newline at EOL.
 * *n* is the number of characters already read.
 */
FL int         readline_restart(FILE *ibuf, char **linebuf, size_t *linesize,
                  size_t n SMALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define readline_restart(A,B,C,D) \
   readline_restart(A, B, C, D, __FILE__, __LINE__)
#endif

/* Read a complete line of input (with editing if possible).
 * If *prompt* is NULL we'll call getprompt() first.
 * Return number of octets or a value <0 on error */
FL int         readline_input(enum lned_mode lned, char const *prompt,
                  char **linebuf, size_t *linesize SMALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define readline_input(A,B,C,D) readline_input(A, B, C, D, __FILE__, __LINE__)
#endif

/* Read a line of input (with editing if possible) and return it savestr()d,
 * or NULL in case of errors or if an empty line would be returned.
 * This may only be called from toplevel (not during sourcing).
 * If *prompt* is NULL we'll call getprompt().
 * *string* is the default/initial content of the return value (this is
 * "almost" ignored in non-interactive mode for reproducability) */
FL char *      readstr_input(char const *prompt, char const *string);

FL void        setptr(FILE *ibuf, off_t offset);
FL int         putline(FILE *obuf, char *linebuf, size_t count);
FL FILE *      setinput(struct mailbox *mp, struct message *m,
                  enum needspec need);
FL struct message * setdot(struct message *mp);
FL int         rm(char *name);
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

/* vars.c hook: *folder* variable has been updated; if *folder* shouldn't be
 * replaced by something else, leave *store* alone, otherwise smalloc() the
 * desired value (ownership will be taken) */
FL bool_t      var_folder_updated(char const *folder, char **store);

/* Determine the current *folder* name, store it in *name* */
FL bool_t      getfold(char *name, size_t size);

FL char const * getdeadletter(void);

FL enum okay   get_body(struct message *mp);

#ifdef HAVE_SOCKETS
FL int         sclose(struct sock *sp);
FL enum okay   swrite(struct sock *sp, const char *data);
FL enum okay   swrite1(struct sock *sp, const char *data, int sz,
                  int use_buffer);
FL enum okay   sopen(const char *xserver, struct sock *sp, int use_ssl,
                  const char *uhp, const char *portstr, int verbose);

/*  */
FL int         sgetline(char **line, size_t *linesize, size_t *linelen,
                  struct sock *sp SMALLOC_DEBUG_ARGS);
# ifdef HAVE_DEBUG
#  define sgetline(A,B,C,D)      sgetline(A, B, C, D, __FILE__, __LINE__)
# endif
#endif

/* Deal with loading of resource files and dealing with a stack of files for
 * the source command */

/* Load a file of user definitions */
FL void        load(char const *name);

/* Pushdown current input file and switch to a new one.  Set the global flag
 * *sourcing* so that others will realize that they are no longer reading from
 * a tty (in all probability) */
FL int         csource(void *v);

/* Pop the current input back to the previous level.  Update the *sourcing*
 * flag as appropriate */
FL int         unstack(void);

/* head.c */

/* Fill in / reedit the desired header fields */
FL int         grab_headers(struct header *hp, enum gfield gflags,
                  int subjfirst);

/* Return the user's From: address(es) */
FL char const * myaddrs(struct header *hp);
/* Boil the user's From: addresses down to a single one, or use *sender* */
FL char const * myorigin(struct header *hp);

/* See if the passed line buffer, which may include trailing newline (sequence)
 * is a mail From_ header line according to RFC 4155 */
FL int         is_head(char const *linebuf, size_t linelen);

/* Savage extract date field from From_ line.  *linelen* is convenience as line
 * must be terminated (but it may end in a newline [sequence]).
 * Return wether the From_ line was parsed successfully */
FL int         extract_date_from_from_(char const *line, size_t linelen,
                  char datebuf[FROM_DATEBUF]);

FL void        extract_header(FILE *fp, struct header *hp);
#define hfieldX(a, b)            hfield_mult(a, b, 1)
#define hfield1(a, b)            hfield_mult(a, b, 0)
FL char *      hfield_mult(char const *field, struct message *mp, int mult);
FL char const * thisfield(char const *linebuf, char const *field);
FL char *      nameof(struct message *mp, int reptype);
FL char const * skip_comment(char const *cp);
FL char const * routeaddr(char const *name);
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

FL char *      realname(char const *name);
FL char *      name1(struct message *mp, int reptype);
FL int         msgidcmp(const char *s1, const char *s2);
FL int         is_ign(char const *field, size_t fieldlen,
                  struct ignoretab ignore[2]);
FL int         member(char const *realfield, struct ignoretab *table);
FL char const * fakefrom(struct message *mp);
FL char const * fakedate(time_t t);
FL time_t      unixtime(char const *from);
FL time_t      rfctime(char const *date);
FL time_t      combinetime(int year, int month, int day,
                  int hour, int minute, int second);
FL void        substdate(struct message *m);
FL int         check_from_and_sender(struct name *fromfield,
                  struct name *senderfield);
FL char *      getsender(struct message *m);

/* imap.c */
#ifdef HAVE_IMAP
FL char const * imap_fileof(char const *xcp);
FL enum okay   imap_noop(void);
FL enum okay   imap_select(struct mailbox *mp, off_t *size, int *count,
                  const char *mbx);
FL int         imap_setfile(const char *xserver, int newmail, int isedit);
FL enum okay   imap_header(struct message *m);
FL enum okay   imap_body(struct message *m);
FL void        imap_getheaders(int bot, int top);
FL void        imap_quit(void);
FL enum okay   imap_undelete(struct message *m, int n);
FL enum okay   imap_unread(struct message *m, int n);
FL int         imap_imap(void *vp);
FL int         imap_newmail(int autoinc);
FL enum okay   imap_append(const char *xserver, FILE *fp);
FL void        imap_folders(const char *name, int strip);
FL enum okay   imap_copy(struct message *m, int n, const char *name);
FL enum okay   imap_search1(const char *spec, int f);
FL int         imap_thisaccount(const char *cp);
FL enum okay   imap_remove(const char *name);
FL enum okay   imap_rename(const char *old, const char *new);
FL enum okay   imap_dequeue(struct mailbox *mp, FILE *fp);
FL int         cconnect(void *vp);
FL int         cdisconnect(void *vp);
FL int         ccache(void *vp);
FL int         disconnected(const char *file);
FL void        transflags(struct message *omessage, long omsgCount,
                  int transparent);
FL time_t      imap_read_date_time(const char *cp);
FL const char * imap_make_date_time(time_t t);
#else
# define imap_imap               ccmdnotsupp
# define cconnect                ccmdnotsupp
# define cdisconnect             ccmdnotsupp
# define ccache                  ccmdnotsupp
#endif

FL time_t      imap_read_date(const char *cp);
FL char *      imap_quotestr(const char *s);
FL char *      imap_unquotestr(const char *s);

/* imap_search.c */
FL enum okay   imap_search(const char *spec, int f);

/* lex.c */
FL int         setfile(char const *name, int newmail);
FL int         newmailinfo(int omsgCount);
FL void        commands(void);
FL int         execute(char *linebuf, int contxt, size_t linesize);
FL void        setmsize(int sz);
FL void        onintr(int s);
FL void        announce(int printheaders);
FL int         newfileinfo(void);
FL int         getmdot(int newmail);
FL void        initbox(const char *name);

/* Print the docstring of `comm', which may be an abbreviation.
 * Return FAL0 if there is no such command */
#ifdef HAVE_DOCSTRINGS
FL bool_t      print_comm_docstr(char const *comm);
#endif

/* list.c */
FL int         getmsglist(char *buf, int *vector, int flags);
FL int         getrawlist(const char *line, size_t linesize,
                  char **argv, int argc, int echolist);
FL int         first(int f, int m);
FL void        mark(int mesg, int f);

/* lzw.c */
#ifdef HAVE_IMAP
FL int         zwrite(void *cookie, const char *wbp, int num);
FL int         zfree(void *cookie);
FL int         zread(void *cookie, char *rbp, int num);
FL void *      zalloc(FILE *fp);
#endif /* HAVE_IMAP */

/* maildir.c */
FL int         maildir_setfile(const char *name, int newmail, int isedit);
FL void        maildir_quit(void);
FL enum okay   maildir_append(const char *name, FILE *fp);
FL enum okay   maildir_remove(const char *name);

/* mime.c */

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
FL enum mimeenc mime_getenc(char *h);
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

FL void        mime_fromhdr(struct str const *in, struct str *out,
                  enum tdflags flags);
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

/* How many characters of (the complete body) *ln* need to be quoted */
FL size_t      mime_cte_mustquote(char const *ln, size_t lnlen, bool_t ishead);

/* How much space is necessary to encode *len* bytes in QP, worst case.
 * Includes room for terminator */
FL size_t      qp_encode_calc_size(size_t len);

/* If *flags* includes QP_ISHEAD these assume "word" input and use special
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

/* If *rest* is set then decoding will assume body text input (assumes input
 * represents lines, only create output when input didn't end with soft line
 * break [except it finalizes an encoded CRLF pair]), otherwise it is assumed
 * to decode a header strings and (1) uses special decoding rules and (b)
 * directly produces output.
 * The buffers of *out* and possibly *rest* will be managed via srealloc().
 * Returns OKAY. XXX or STOP on error (in which case *out* is set to an error
 * XXX message); caller is responsible to free buffers */
FL int         qp_decode(struct str *out, struct str const *in,
                  struct str *rest);

/* How much space is necessary to encode *len* bytes in Base64, worst case.
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

/* If *rest* is set then decoding will assume text input.
 * The buffers of *out* and possibly *rest* will be managed via srealloc().
 * Returns OKAY or STOP on error (in which case *out* is set to an error
 * message); caller is responsible to free buffers */
FL int         b64_decode(struct str *out, struct str const *in,
                  struct str *rest);

/*
 * names.c
 */

FL struct name * nalloc(char *str, enum gfield ntype);
FL struct name * ndup(struct name *np, enum gfield ntype);
FL struct name * cat(struct name *n1, struct name *n2);
FL int         count(struct name const *np);

FL struct name * extract(char const *line, enum gfield ntype);
FL struct name * lextract(char const *line, enum gfield ntype);
FL char *      detract(struct name *np, enum gfield ntype);

/* Get a lextract() list via readstr_input(), reassigning to *np* */
FL struct name * grab_names(const char *field, struct name *np, int comma,
         enum gfield gflags);

FL struct name * checkaddrs(struct name *np);
FL struct name * usermap(struct name *names, bool_t force_metoo);
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

/* openssl.c */
#ifdef HAVE_OPENSSL
FL enum okay   ssl_open(const char *server, struct sock *sp, const char *uhp);
FL void        ssl_gen_err(const char *fmt, ...);
FL int         cverify(void *vp);
FL FILE *      smime_sign(FILE *ip, struct header *);
FL FILE *      smime_encrypt(FILE *ip, const char *certfile, const char *to);
FL struct message * smime_decrypt(struct message *m, const char *to,
                     const char *cc, int signcall);
FL enum okay   smime_certsave(struct message *m, int n, FILE *op);
#else
# define cverify                 ccmdnotsupp
#endif

/* pop3.c */
#ifdef HAVE_POP3
FL enum okay   pop3_noop(void);
FL int         pop3_setfile(const char *server, int newmail, int isedit);
FL enum okay   pop3_header(struct message *m);
FL enum okay   pop3_body(struct message *m);
FL void        pop3_quit(void);
#endif

/*
 * popen.c
 * Subprocesses, popen, but also file handling with registering
 */

FL FILE *      safe_fopen(const char *file, const char *mode, int *omode);
FL FILE *      Fopen(const char *file, const char *mode);
FL FILE *      Fdopen(int fd, const char *mode);
FL int         Fclose(FILE *fp);
FL FILE *      Zopen(const char *file, const char *mode, int *compression);

/* Create a temporary file in tempdir, use prefix for its name, store the
 * unique name in fn, and return a stdio FILE pointer with access mode.
 * *bits* specifies the access mode of the newly created temporary file */
FL FILE *      Ftemp(char **fn, char const *prefix, char const *mode,
                  int bits, int register_file);

/* Free the resources associated with the given filename.  To be called after
 * unlink().  Since this function can be called after receiving a signal, the
 * variable must be made NULL first and then free()d, to avoid more than one
 * free() call in all circumstances */
FL void        Ftfree(char **fn);

/* Create a pipe and ensure CLOEXEC bit is set in both descriptors */
FL bool_t      pipe_cloexec(int fd[2]);

FL FILE *      Popen(const char *cmd, const char *mode, const char *shell,
                  int newfd1);

FL bool_t      Pclose(FILE *ptr, bool_t dowait);

FL void        close_all_files(void);
FL int         run_command(char const *cmd, sigset_t *mask, int infd,
                  int outfd, char const *a0, char const *a1, char const *a2);
FL int         start_command(const char *cmd, sigset_t *mask, int infd,
                  int outfd, const char *a0, const char *a1, const char *a2);
FL void        prepare_child(sigset_t *nset, int infd, int outfd);
FL void        sigchild(int signo);
FL void        free_child(int pid);

/* Wait for pid, return wether we've had a normal EXIT_SUCCESS exit.
 * If wait_status is set, set it to the reported waitpid(2) wait status */
FL bool_t      wait_child(int pid, int *wait_status);

/* quit.c */
FL int         quitcmd(void *v);
FL void        quit(void);
FL int         holdbits(void);
FL void        save_mbox_for_possible_quitstuff(void); /* TODO DROP IF U CAN */
FL enum okay   makembox(void);
FL int         savequitflags(void);
FL void        restorequitflags(int);

/* send.c */
FL int         sendmp(struct message *mp, FILE *obuf, struct ignoretab *doign,
                  char const *prefix, enum sendaction action, off_t *stats);

/* sendout.c */
FL int         mail(struct name *to, struct name *cc, struct name *bcc,
                  char *subject, struct attachment *attach,
                  char *quotefile, int recipient_record);
FL int         csendmail(void *v);
FL int         cSendmail(void *v);
FL enum        okay mail1(struct header *hp, int printheaders,
                  struct message *quote, char *quotefile, int recipient_record,
                  int doprefix);
FL int         mkdate(FILE *fo, const char *field);
FL int         puthead(struct header *hp, FILE *fo, enum gfield w,
                  enum sendaction action, enum conversion convert,
                  char const *contenttype, char const *charset);
FL enum okay   resend_msg(struct message *mp, struct name *to, int add_resent);

/*
 * smtp.c
 */

#ifdef HAVE_SMTP
FL char *      smtp_auth_var(const char *type, const char *addr);
FL int         smtp_mta(char *server, struct name *to, FILE *fi,
                  struct header *hp, const char *user, const char *password,
                  const char *skinned);
#endif

/*
 * spam.c
 */

#ifdef HAVE_SPAM
FL int         cspam_clear(void *v);
FL int         cspam_set(void *v);
FL int         cspam_forget(void *v);
FL int         cspam_ham(void *v);
FL int         cspam_rate(void *v);
FL int         cspam_spam(void *v);
#else
# define cspam_clear             ccmdnotsupp
# define cspam_set               ccmdnotsupp
# define cspam_forget            ccmdnotsupp
# define cspam_ham               ccmdnotsupp
# define cspam_rate              ccmdnotsupp
# define cspam_spam              ccmdnotsupp
#endif

/* ssl.c */
#ifdef HAVE_SSL
FL void        ssl_set_vrfy_level(const char *uhp);
FL enum okay   ssl_vrfy_decide(void);
FL char *      ssl_method_string(const char *uhp);
FL enum okay   smime_split(FILE *ip, FILE **hp, FILE **bp, long xcount,
                  int keep);
FL FILE *      smime_sign_assemble(FILE *hp, FILE *bp, FILE *sp);
FL FILE *      smime_encrypt_assemble(FILE *hp, FILE *yp);
FL struct message * smime_decrypt_assemble(struct message *m, FILE *hp,
                     FILE *bp);
FL int         ccertsave(void *v);
FL enum okay   rfc2595_hostname_match(const char *host, const char *pattern);
#else
# define ccertsave               ccmdnotsupp
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
#define strcomma(IOL,IGN)        n_strsep(IOL, ',', IGN)

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
FL int         substr(const char *str, const char *sub);

/* Lazy vsprintf wrapper */
#ifndef HAVE_SNPRINTF
FL int         snprintf(char *str, size_t size, const char *format, ...);
#endif

FL char *      sstpcpy(char *dst, const char *src);
FL char *      sstrdup(char const *cp SMALLOC_DEBUG_ARGS);
FL char *      sbufdup(char const *cp, size_t len SMALLOC_DEBUG_ARGS);
#ifdef HAVE_DEBUG
# define sstrdup(CP)             sstrdup(CP, __FILE__, __LINE__)
# define sbufdup(CP,L)           sbufdup(CP, L, __FILE__, __LINE__)
#endif

FL char *      n_strlcpy(char *dst, const char *src, size_t len);

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

/* thread.c */
FL int         thread(void *vp);
FL int         unthread(void *vp);
FL struct message * next_in_thread(struct message *mp);
FL struct message * prev_in_thread(struct message *mp);
FL struct message * this_in_thread(struct message *mp, long n);
FL int         sort(void *vp);
FL int         ccollapse(void *v);
FL int         cuncollapse(void *v);
FL void        uncollapse1(struct message *m, int always);

/*
 * tty.c
 */

/* [Yy]es or [Nn]o */
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
