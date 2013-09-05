/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Exported function prototypes.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
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
#define n_strlcpy(a,b,c)	(strncpy(a, b, c), a[c - 1] = '\0')

/*
 * attachments.c
 */

/* Try to add an attachment for *file*, file_expand()ed.
 * Return the new head of list *aphead*, or NULL.
 * The newly created attachment will be stored in **newap*, if given */
struct attachment *	add_attachment(struct attachment *aphead, char *file,
				struct attachment **newap);

/* Append comma-separated list of file names to the end of attachment list */
struct attachment *	append_attachments(struct attachment *aphead,
				char *names);

/* Interactively edit the attachment list, return the new list head */
struct attachment *	edit_attachments(struct attachment *aphead);

/*
 * auxlily.c
 */

/* Announce a fatal error and die */
void	panic(const char *format, ...);

/* Hold *all* signals, and release that total block again */
void	hold_all_sigs(void);
void	rele_all_sigs(void);

void holdint(void);
void relseint(void);
void touch(struct message *mp);
int is_dir(char const *name);
int argcount(char **argv);
char *colalign(const char *cp, int col, int fill);

/* Check wether using a pager is possible/makes sense and is desired by user
 * (*crt* set); return number of screen lines (or *crt*) if so, 0 otherwise */
size_t	paging_seems_sensible(void);

/* Use a pager or STDOUT to print *fp*; if *lines* is 0, they'll be counted */
void	page_or_print(FILE *fp, size_t lines);
#define try_pager(FP)		page_or_print(FP, 0) /* TODO obsolete */

enum protocol which_protocol(const char *name);
unsigned pjw(const char *cp);
long nextprime(long n);

/* Check wether *s is an escape sequence, expand it as necessary.
 * Returns the expanded sequence or 0 if **s is NUL or -1 if it is \c.
 * *s is advanced to after the expanded sequence (as possible) */
int	expand_shell_escape(char const **s);

/* Get *prompt*, or '& ' if *bsdcompat*, of '? ' otherwise */
char *	getprompt(void);

/* Search passwd file for a uid, return name on success, NULL on failure */
char *	getname(int uid);
/* Discover user login name */
char *	username(void);
/* Return our hostname */
char *	nodename(int mayoverride);

/* Try to lookup a variable named "password-*token*".
 * Return NULL or salloc()ed buffer */
char *	lookup_password_for_token(char const *token);

/* Get a (pseudo) random string of *length* bytes; returns salloc()ed buffer */
char *	getrandstring(size_t length);

#define	Hexchar(n)		((n)>9 ? (n)-10+'A' : (n)+'0')
#define	hexchar(n)		((n)>9 ? (n)-10+'a' : (n)+'0')

#ifdef USE_MD5
/* MD5 checksum as hexadecimal string, to be stored in *hex* */
#define MD5TOHEX_SIZE		32
char *	md5tohex(char hex[MD5TOHEX_SIZE], void const *vp);

/* CRAM-MD5 encode the *user* / *pass* / *b64* combo */
char *	cram_md5_string(char const *user, char const *pass, char const *b64);
#endif

enum okay makedir(const char *name);
enum okay cwget(struct cw *cw);
enum okay cwret(struct cw *cw);
void cwrelse(struct cw *cw);
void makeprint(struct str const *in, struct str *out);
char *prstr(const char *s);
int prout(const char *s, size_t sz, FILE *fp);
int putuc(int u, int c, FILE *fp);

/* Update *tc* to now; only .tc_time updated unless *full_update* is true */
void	time_current_update(struct time_current *tc, bool_t full_update);

/* getopt(3) fallback implementation */
#ifndef HAVE_GETOPT
char	*my_optarg;
int	my_optind, /*my_opterr,*/ my_optopt;

int	my_getopt(int argc, char *const argv[], const char *optstring);
# define getopt		my_getopt
# define optarg		my_optarg
# define optind		my_optind
/*# define opterr		my_opterr*/
# define optopt		my_optopt
#endif

/* Memory allocation routines */
#ifdef HAVE_ASSERTS
# define SMALLOC_DEBUG_ARGS	, char const *mdbg_file, int mdbg_line
# define SMALLOC_DEBUG_ARGSCALL	, mdbg_file, mdbg_line
#else
# define SMALLOC_DEBUG_ARGS
# define SMALLOC_DEBUG_ARGSCALL
#endif

void *	smalloc(size_t s SMALLOC_DEBUG_ARGS);
void *	srealloc(void *v, size_t s SMALLOC_DEBUG_ARGS);
void *	scalloc(size_t nmemb, size_t size SMALLOC_DEBUG_ARGS);

#ifdef HAVE_ASSERTS
void	sfree(void *v SMALLOC_DEBUG_ARGS);
/* Called by sreset(), then */
void	smemreset(void);
/* The *smemtrace* command */
int	smemtrace(void *v);
# define smalloc(SZ)		smalloc(SZ, __FILE__, __LINE__)
# define srealloc(P,SZ)		srealloc(P, SZ, __FILE__, __LINE__)
# define scalloc(N,SZ)		scalloc(N, SZ, __FILE__, __LINE__)
# define free(P)		sfree(P, __FILE__, __LINE__)
#else
# define smemtrace		do {} while (0)
#endif

/* cache.c */
enum okay getcache1(struct mailbox *mp, struct message *m,
		enum needspec need, int setflags);
enum okay getcache(struct mailbox *mp, struct message *m, enum needspec need);
void putcache(struct mailbox *mp, struct message *m);
void initcache(struct mailbox *mp);
void purgecache(struct mailbox *mp, struct message *m, long mc);
void delcache(struct mailbox *mp, struct message *m);
enum okay cache_setptr(int transparent);
enum okay cache_list(struct mailbox *mp, const char *base, int strip, FILE *fp);
enum okay cache_remove(const char *name);
enum okay cache_rename(const char *old, const char *new);
unsigned long cached_uidvalidity(struct mailbox *mp);
FILE *cache_queue(struct mailbox *mp);
enum okay cache_dequeue(struct mailbox *mp);

/* cmd1.c */
int ccmdnotsupp(void *v);
char const *get_pager(void);
int headers(void *v);
int scroll(void *v);
int Scroll(void *v);
int screensize(void);
int from(void *v);

/* Print out the header of a specific message.
 * Note: ensure to call time_current_update() before first use in cycle! */
void printhead(int mesg, FILE *f, int threaded);

int pdot(void *v);
int pcmdlist(void *v);
char *laststring(char *linebuf, int *flag, int strip);
int more(void *v);
int More(void *v);
int type(void *v);
int Type(void *v);
int show(void *v);
int pipecmd(void *v);
int Pipecmd(void *v);
int top(void *v);
int stouch(void *v);
int mboxit(void *v);
int folders(void *v);

/* cmd2.c */
int next(void *v);
int save(void *v);
int Save(void *v);
int copycmd(void *v);
int Copycmd(void *v);
int cmove(void *v);
int cMove(void *v);
int cdecrypt(void *v);
int cDecrypt(void *v);
int clobber(void *v);
int core(void *v);
int cwrite(void *v);
int delete(void *v);
int deltype(void *v);
int undeletecmd(void *v);
int retfield(void *v);
int igfield(void *v);
int saveretfield(void *v);
int saveigfield(void *v);
int fwdretfield(void *v);
int fwdigfield(void *v);
int unignore(void *v);
int unretain(void *v);
int unsaveignore(void *v);
int unsaveretain(void *v);
int unfwdignore(void *v);
int unfwdretain(void *v);

/* cmd3.c */
int shell(void *v);
int dosh(void *v);
int help(void *v);
int schdir(void *v);
int respond(void *v);
int respondall(void *v);
int respondsender(void *v);
int followup(void *v);
int followupall(void *v);
int followupsender(void *v);
int preserve(void *v);
int unread(void *v);
int seen(void *v);
int messize(void *v);
int rexit(void *v);
/* Set or display a variable value.  Syntax is similar to that of sh */
int	set(void *v);
int unset(void *v);
int group(void *v);
int ungroup(void *v);
int cfile(void *v);
int echo(void *v);
int Respond(void *v);
int Followup(void *v);
int forwardcmd(void *v);
int Forwardcmd(void *v);
int ifcmd(void *v);
int elsecmd(void *v);
int endifcmd(void *v);
int alternates(void *v);
int resendcmd(void *v);
int Resendcmd(void *v);
int newmail(void *v);
int shortcut(void *v);
struct shortcut *get_shortcut(const char *str);
int unshortcut(void *v);
struct oldaccount *get_oldaccount(const char *name);
int account(void *v);
int cflag(void *v);
int cunflag(void *v);
int canswered(void *v);
int cunanswered(void *v);
int cdraft(void *v);
int cundraft(void *v);
#ifdef USE_SCORE
int ckill(void *v);
int cunkill(void *v);
int cscore(void *v);
#else
# define ckill		ccmdnotsupp
# define cunkill	ccmdnotsupp
# define cscore		ccmdnotsupp
#endif
int cnoop(void *v);
int cremove(void *v);
int crename(void *v);

/* cmdtab.c */

/* collect.c */

FILE *collect(struct header *hp, int printheaders, struct message *mp,
		char *quotefile, int doprefix);

void	savedeadletter(FILE *fp, int fflush_rewind_first);

/* dotlock.c */
int fcntl_lock(int fd, int type);
int dot_lock(const char *fname, int fd, int pollinterval, FILE *fp,
		const char *msg);
void dot_unlock(const char *fname);

/* edit.c */
int editor(void *v);
int visual(void *v);
FILE *run_editor(FILE *fp, off_t size, int type, int readonly,
		struct header *hp, struct message *mp, enum sendaction action,
		sighandler_type oldint);

/*
 * filter.c
 */

/* Quote filter */
struct quoteflt *	quoteflt_dummy(void); /* TODO LEGACY */
void	quoteflt_init(struct quoteflt *self, char const *prefix);
void	quoteflt_destroy(struct quoteflt *self);
void	quoteflt_reset(struct quoteflt *self, FILE *f);
ssize_t	quoteflt_push(struct quoteflt *self, char const *dat, size_t len);
ssize_t	quoteflt_flush(struct quoteflt *self);

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
char *		fgetline(char **line, size_t *linesize, size_t *count,
			size_t *llen, FILE *fp, int appendnl
			SMALLOC_DEBUG_ARGS);
#ifdef HAVE_ASSERTS
# define fgetline(A,B,C,D,E,F)	\
	fgetline(A, B, C, D, E, F, __FILE__, __LINE__)
#endif

/* Read up a line from the specified input into the linebuffer.
 * Return the number of characters read.  Do not include the newline at EOL.
 * *n* is the number of characters already read.
 */
int		readline_restart(FILE *ibuf, char **linebuf, size_t *linesize,
			size_t n SMALLOC_DEBUG_ARGS);
#ifdef HAVE_ASSERTS
# define readline_restart(A,B,C,D) \
	readline_restart(A, B, C, D, __FILE__, __LINE__)
#endif

/* Read a complete line of input (with editing if possible).
 * If *prompt* is NULL we'll call getprompt() first.
 * Return number of octets or a value <0 on error */
int		readline_input(enum lned_mode lned, char const *prompt,
			char **linebuf, size_t *linesize SMALLOC_DEBUG_ARGS);
#ifdef HAVE_ASSERTS
# define readline_input(A,B,C,D) readline_input(A, B, C, D, __FILE__, __LINE__)
#endif

/* Read a line of input (with editing if possible) and return it savestr()d,
 * or NULL in case of errors or if an empty line would be returned.
 * This may only be called from toplevel (not during sourcing).
 * If *prompt* is NULL we'll call getprompt().
 * *string* is the default/initial content of the return value (this is
 * "almost" ignored in non-interactive mode for reproducability) */
char *		readstr_input(char const *prompt, char const *string);

void setptr(FILE *ibuf, off_t offset);
int putline(FILE *obuf, char *linebuf, size_t count);
FILE *setinput(struct mailbox *mp, struct message *m, enum needspec need);
struct message *setdot(struct message *mp);
int rm(char *name);
void holdsigs(void);
void relsesigs(void);
off_t fsize(FILE *iob);

/* Evaluate the string given as a new mailbox name. Supported meta characters:
 *	%	for my system mail box
 *	%user	for user's system mail box
 *	#	for previous file
 *	&	invoker's mbox file
 *	+file	file in folder directory
 *	any shell meta character
 * Returns the file name as an auto-reclaimed string */
char *	fexpand(char const *name, enum fexp_mode fexpm);

#define expand(N)	fexpand(N, FEXP_FULL)	/* XXX obsolete */
#define file_expand(N)	fexpand(N, FEXP_LOCAL)	/* XXX obsolete */

/* Get rid of queued mail */
void	demail(void);

/* vars.c hook: *folder* variable has been updated; if *folder* shouldn't be
 * replaced by something else, leave *store* alone, otherwise smalloc() the
 * desired value (ownership will be taken) */
bool_t	var_folder_updated(char const *folder, char **store);

/* Determine the current *folder* name, store it in *name* */
bool_t	getfold(char *name, size_t size);

char const *getdeadletter(void);

void newline_appended(void);
enum okay get_body(struct message *mp);

#ifdef HAVE_SOCKETS
int sclose(struct sock *sp);
enum okay swrite(struct sock *sp, const char *data);
enum okay swrite1(struct sock *sp, const char *data, int sz, int use_buffer);
enum okay sopen(const char *xserver, struct sock *sp, int use_ssl,
		const char *uhp, const char *portstr, int verbose);

/*  */
int		sgetline(char **line, size_t *linesize, size_t *linelen,
			struct sock *sp SMALLOC_DEBUG_ARGS);
# ifdef HAVE_ASSERTS
#  define sgetline(A,B,C,D)	sgetline(A, B, C, D, __FILE__, __LINE__)
# endif
#endif

/* Deal with loading of resource files and dealing with a stack of files for
 * the source command */

/* Load a file of user definitions */
void		load(char const *name);

/* Pushdown current input file and switch to a new one.  Set the global flag
 * *sourcing* so that others will realize that they are no longer reading from
 * a tty (in all probability) */
int		csource(void *v);

/* Pop the current input back to the previous level.  Update the *sourcing*
 * flag as appropriate */
int		unstack(void);

/* head.c */

/* Fill in / reedit the desired header fields */
int		grab_headers(struct header *hp, enum gfield gflags,
			int subjfirst);

/* Return the user's From: address(es) */
char const *	myaddrs(struct header *hp);
/* Boil the user's From: addresses down to a single one, or use *sender* */
char const *	myorigin(struct header *hp);

/* See if the passed line buffer, which may include trailing newline (sequence)
 * is a mail From_ header line according to RFC 4155 */
int	is_head(char const *linebuf, size_t linelen);

/* Savage extract date field from From_ line.  *linelen* is convenience as line
 * must be terminated (but it may end in a newline [sequence]).
 * Return wether the From_ line was parsed successfully */
int	extract_date_from_from_(char const *line, size_t linelen,
		char datebuf[FROM_DATEBUF]);

void extract_header(FILE *fp, struct header *hp);
#define	hfieldX(a, b)	hfield_mult(a, b, 1)
#define	hfield1(a, b)	hfield_mult(a, b, 0)
char *hfield_mult(char const *field, struct message *mp, int mult);
char const *thisfield(char const *linebuf, char const *field);
char *nameof(struct message *mp, int reptype);
char const *skip_comment(char const *cp);
char const *routeaddr(char const *name);
int is_addr_invalid(struct name *np, int putmsg);

/* Does *NP* point to a file or pipe addressee? */
#define is_fileorpipe_addr(NP) \
	(((NP)->n_flags & NAME_ADDRSPEC_ISFILEORPIPE) != 0)

/* Return skinned version of *NP*s name */
#define skinned_name(NP) \
	(assert((NP)->n_flags & NAME_SKINNED), \
	((struct name const*)NP)->n_name)

/* Skin an address according to the RFC 822 interpretation of "host-phrase" */
char *	skin(char const *name);

/* Skin *name* and extract the *addr-spec* according to RFC 5322.
 * Store the result in .ag_skinned and also fill in those .ag_ fields that have
 * actually been seen.
 * Return 0 if something good has been parsed, 1 if fun didn't exactly know how
 * to deal with the input, or if that was plain invalid */
int	addrspec_with_guts(int doskin, char const *name, struct addrguts *agp);

char *realname(char const *name);
char *name1(struct message *mp, int reptype);
int msgidcmp(const char *s1, const char *s2);
int is_ign(char const *field, size_t fieldlen, struct ignoretab ignore[2]);
int member(char const *realfield, struct ignoretab *table);
char const *fakefrom(struct message *mp);
char const *fakedate(time_t t);
time_t unixtime(char const *from);
time_t rfctime(char const *date);
time_t combinetime(int year, int month, int day,
		int hour, int minute, int second);
void substdate(struct message *m);
int check_from_and_sender(struct name *fromfield, struct name *senderfield);
char *getsender(struct message *m);

/* imap.c */
#ifdef USE_IMAP
char const *	imap_fileof(char const *xcp);
enum okay imap_noop(void);
enum okay imap_select(struct mailbox *mp, off_t *size, int *count,
		const char *mbx);
int imap_setfile(const char *xserver, int newmail, int isedit);
enum okay imap_header(struct message *m);
enum okay imap_body(struct message *m);
void imap_getheaders(int bot, int top);
void imap_quit(void);
enum okay imap_undelete(struct message *m, int n);
enum okay imap_unread(struct message *m, int n);
int imap_imap(void *vp);
int imap_newmail(int autoinc);
enum okay imap_append(const char *xserver, FILE *fp);
void imap_folders(const char *name, int strip);
enum okay imap_copy(struct message *m, int n, const char *name);
enum okay imap_search1(const char *spec, int f);
int imap_thisaccount(const char *cp);
enum okay imap_remove(const char *name);
enum okay imap_rename(const char *old, const char *new);
enum okay imap_dequeue(struct mailbox *mp, FILE *fp);
int cconnect(void *vp);
int cdisconnect(void *vp);
int ccache(void *vp);
int	disconnected(const char *file);
void	transflags(struct message *omessage, long omsgCount, int transparent);
#else
# define imap_imap	ccmdnotsupp
# define cconnect	ccmdnotsupp
# define cdisconnect	ccmdnotsupp
# define ccache		ccmdnotsupp
#endif

time_t imap_read_date_time(const char *cp);
time_t imap_read_date(const char *cp);
const char *imap_make_date_time(time_t t);
char *imap_quotestr(const char *s);
char *imap_unquotestr(const char *s);

/* imap_gssapi.c */

/* imap_search.c */
enum okay imap_search(const char *spec, int f);

/* junk.c */
#ifdef USE_JUNK
int cgood(void *v);
int cjunk(void *v);
int cungood(void *v);
int cunjunk(void *v);
int cclassify(void *v);
int cprobability(void *v);
#else
# define cgood		ccmdnotsupp
# define cjunk		ccmdnotsupp
# define cungood	ccmdnotsupp
# define cunjunk	ccmdnotsupp
# define cclassify	ccmdnotsupp
# define cprobability	ccmdnotsupp
#endif

/* lex.c */
int setfile(char const *name, int newmail);
int newmailinfo(int omsgCount);
void commands(void);
int execute(char *linebuf, int contxt, size_t linesize);
void setmsize(int sz);
void onintr(int s);
void announce(int printheaders);
int newfileinfo(void);
int getmdot(int newmail);
int pversion(void *v);
void initbox(const char *name);

/* list.c */
int getmsglist(char *buf, int *vector, int flags);
int getrawlist(const char *line, size_t linesize,
		char **argv, int argc, int echolist);
int first(int f, int m);
void mark(int mesg, int f);

/* lzw.c */
int zwrite(void *cookie, const char *wbp, int num);
int zfree(void *cookie);
int zread(void *cookie, char *rbp, int num);
void *zalloc(FILE *fp);

/* maildir.c */
int maildir_setfile(const char *name, int newmail, int isedit);
void maildir_quit(void);
enum okay maildir_append(const char *name, FILE *fp);
enum okay maildir_remove(const char *name);

/* main.c */
int main(int argc, char *argv[]);

/* mime.c */

/* *charset-7bit*, else CHARSET_7BIT */
char const *	charset_get_7bit(void);

/* *charset-8bit*, else CHARSET_8BIT */
char const *	charset_get_8bit(void);

/* LC_CTYPE:CODESET / *ttycharset*, else *charset-8bit*, else CHARSET_8BIT */
char const *	charset_get_lc(void);

/* *sendcharsets* / *charset-8bit* iterator.
 * *a_charset_to_try_first* may be used to prepend a charset (as for
 * *reply-in-same-charset*);  works correct for !HAVE_ICONV */
void		charset_iter_reset(char const *a_charset_to_try_first);
char const *	charset_iter_next(void);
char const *	charset_iter_current(void);
void charset_iter_recurse(char *outer_storage[2]); /* TODO LEGACY FUN, REMOVE */
void charset_iter_restore(char *outer_storage[2]); /* TODO LEGACY FUN, REMOVE */

char const *need_hdrconv(struct header *hp, enum gfield w);
enum mimeenc mime_getenc(char *h);
char *mime_getparam(char const *param, char *h);

/* Get the boundary out of a Content-Type: multipart/xyz header field, return
 * salloc()ed copy of it; store strlen() in *len if set */
char *		mime_get_boundary(char *h, size_t *len);

/* Create a salloc()ed MIME boundary */
char *		mime_create_boundary(void);

/* Classify content of *fp* as necessary and fill in arguments; **charset* is
 * left alone unless it's non-NULL */
int		mime_classify_file(FILE *fp, char const **contenttype,
			char const **charset, int *do_iconv);

/* */
enum mimecontent mime_classify_content_of_part(struct mimepart const *mip);

/* Return the Content-Type matching the extension of name */
char *		mime_classify_content_type_by_fileext(char const *name);

/* "mimetypes" command */
int		cmimetypes(void *v);

void mime_fromhdr(struct str const *in, struct str *out, enum tdflags flags);
char *mime_fromaddr(char const *name);

/* fwrite(3) performing the given MIME conversion */
ssize_t		mime_write(char const *ptr, size_t size, FILE *f,
			enum conversion convert, enum tdflags dflags,
			struct quoteflt *qf, struct str *rest);
ssize_t		xmime_write(char const *ptr, size_t size, /* TODO LEGACY */
			FILE *f, enum conversion convert, enum tdflags dflags,
			struct str *rest);

/*
 * mime_cte.c
 * Content-Transfer-Encodings as defined in RFC 2045:
 * - Quoted-Printable, section 6.7
 * - Base64, section 6.8
 */

/* How many characters of (the complete body) *ln* need to be quoted */
size_t		mime_cte_mustquote(char const *ln, size_t lnlen, bool_t ishead);

/* How much space is necessary to encode *len* bytes in QP, worst case.
 * Includes room for terminator */
size_t		qp_encode_calc_size(size_t len);

/* If *flags* includes QP_ISHEAD these assume "word" input and use special
 * quoting rules in addition; soft line breaks are not generated.
 * Otherwise complete input lines are assumed and soft line breaks are
 * generated as necessary */
struct str *	qp_encode(struct str *out, struct str const *in,
			enum qpflags flags);
struct str *	qp_encode_cp(struct str *out, char const *cp,
			enum qpflags flags);
struct str *	qp_encode_buf(struct str *out, void const *vp, size_t vp_len,
			enum qpflags flags);

/* If *rest* is set then decoding will assume body text input (assumes input
 * represents lines, only create output when input didn't end with soft line
 * break [except it finalizes an encoded CRLF pair]), otherwise it is assumed
 * to decode a header strings and (1) uses special decoding rules and (b)
 * directly produces output.
 * The buffers of *out* and possibly *rest* will be managed via srealloc().
 * Returns OKAY. XXX or STOP on error (in which case *out* is set to an error
 * XXX message); caller is responsible to free buffers */
int		qp_decode(struct str *out, struct str const *in,
			struct str *rest);

/* How much space is necessary to encode *len* bytes in Base64, worst case.
 * Includes room for terminator */
size_t		b64_encode_calc_size(size_t len);

/* Note these simply convert all the input (if possible), including the
 * insertion of NL sequences if B64_CRLF or B64_LF is set (and multiple thereof
 * if B64_MULTILINE is set).
 * Thus, in the B64_BUF case, better call b64_encode_calc_size() first */
struct str *	b64_encode(struct str *out, struct str const *in,
			enum b64flags flags);
struct str *	b64_encode_cp(struct str *out, char const *cp,
			enum b64flags flags);
struct str *	b64_encode_buf(struct str *out, void const *vp, size_t vp_len,
			enum b64flags flags);

/* If *rest* is set then decoding will assume text input.
 * The buffers of *out* and possibly *rest* will be managed via srealloc().
 * Returns OKAY or STOP on error (in which case *out* is set to an error
 * message); caller is responsible to free buffers */
int		b64_decode(struct str *out, struct str const *in,
			struct str *rest);

/*
 * names.c
 */

struct name *	nalloc(char *str, enum gfield ntype);
struct name *	ndup(struct name *np, enum gfield ntype);
struct name *	cat(struct name *n1, struct name *n2);
int		count(struct name const *np);

struct name *	extract(char const *line, enum gfield ntype);
struct name *	lextract(char const *line, enum gfield ntype);
char *		detract(struct name *np, enum gfield ntype);

/* Get a lextract() list via readstr_input(), reassigning to *np* */
struct name *	grab_names(const char *field, struct name *np, int comma,
			enum gfield gflags);

struct name *	checkaddrs(struct name *np);
struct name *	usermap(struct name *names, bool_t force_metoo);
struct name *	elide(struct name *names);
struct name *	delete_alternates(struct name *np);
int		is_myname(char const *name);

/* Dispatch a message to all pipe and file addresses TODO -> sendout.c */
struct name *	outof(struct name *names, FILE *fo, struct header *hp,
			bool_t *senderror);

/* Handling of alias groups */

/* Locate a group name and return it */
struct grouphead *findgroup(char *name);

/* Print a group out on stdout */
void		printgroup(char *name);

void		remove_group(char const *name);

/* openssl.c */
#ifdef USE_OPENSSL
enum okay ssl_open(const char *server, struct sock *sp, const char *uhp);
void ssl_gen_err(const char *fmt, ...);
int cverify(void *vp);
FILE *smime_sign(FILE *ip, struct header *);
FILE *smime_encrypt(FILE *ip, const char *certfile, const char *to);
struct message *smime_decrypt(struct message *m, const char *to,
		const char *cc, int signcall);
enum okay smime_certsave(struct message *m, int n, FILE *op);
#else
# define cverify	ccmdnotsupp
#endif

/* pop3.c */
#ifdef USE_POP3
enum okay pop3_noop(void);
int pop3_setfile(const char *server, int newmail, int isedit);
enum okay pop3_header(struct message *m);
enum okay pop3_body(struct message *m);
void pop3_quit(void);
#endif

/*
 * popen.c
 * Subprocesses, popen, but also file handling with registering
 */

sighandler_type safe_signal(int signum, sighandler_type handler);
FILE *safe_fopen(const char *file, const char *mode, int *omode);
FILE *Fopen(const char *file, const char *mode);
FILE *Fdopen(int fd, const char *mode);
int Fclose(FILE *fp);
FILE *Zopen(const char *file, const char *mode, int *compression);

/* Create a temporary file in tempdir, use prefix for its name, store the
 * unique name in fn, and return a stdio FILE pointer with access mode.
 * *bits* specifies the access mode of the newly created temporary file */
FILE *	Ftemp(char **fn, char const *prefix, char const *mode,
		int bits, int register_file);

/* Free the resources associated with the given filename.  To be called after
 * unlink().  Since this function can be called after receiving a signal, the
 * variable must be made NULL first and then free()d, to avoid more than one
 * free() call in all circumstances */
void	Ftfree(char **fn);

/* Create a pipe and ensure CLOEXEC bit is set in both descriptors */
bool_t	pipe_cloexec(int fd[2]);

FILE *Popen(const char *cmd, const char *mode, const char *shell, int newfd1);
int Pclose(FILE *ptr, bool_t dowait);
void close_all_files(void);
int run_command(char const *cmd, sigset_t *mask, int infd, int outfd,
		char const *a0, char const *a1, char const *a2);
int start_command(const char *cmd, sigset_t *mask, int infd, int outfd,
		const char *a0, const char *a1, const char *a2);
void prepare_child(sigset_t *nset, int infd, int outfd);
void sigchild(int signo);
void free_child(int pid);
int wait_child(int pid);

/* quit.c */
int quitcmd(void *v);
void quit(void);
int holdbits(void);
enum okay makembox(void);
int savequitflags(void);
void restorequitflags(int);

/* send.c */
#undef send
#define send(a, b, c, d, e, f)  xsend(a, b, c, d, e, f)
int send(struct message *mp, FILE *obuf, struct ignoretab *doign,
		char const *prefix, enum sendaction action, off_t *stats);

/* sendout.c */
int mail(struct name *to, struct name *cc, struct name *bcc,
		char *subject, struct attachment *attach,
		char *quotefile, int recipient_record);
int sendmail(void *v);
int Sendmail(void *v);
enum okay mail1(struct header *hp, int printheaders, struct message *quote,
		char *quotefile, int recipient_record, int doprefix);
int mkdate(FILE *fo, const char *field);
int puthead(struct header *hp, FILE *fo, enum gfield w,
		enum sendaction action, enum conversion convert,
		char const *contenttype, char const *charset);
enum okay resend_msg(struct message *mp, struct name *to, int add_resent);

/*
 * smtp.c
 */

#ifdef USE_SMTP
char *	smtp_auth_var(const char *type, const char *addr);
int	smtp_mta(char *server, struct name *to, FILE *fi, struct header *hp,
		const char *user, const char *password, const char *skinned);
#endif

/* ssl.c */
#ifdef USE_SSL
void ssl_set_vrfy_level(const char *uhp);
enum okay ssl_vrfy_decide(void);
char *ssl_method_string(const char *uhp);
enum okay smime_split(FILE *ip, FILE **hp, FILE **bp, long xcount, int keep);
FILE *smime_sign_assemble(FILE *hp, FILE *bp, FILE *sp);
FILE *smime_encrypt_assemble(FILE *hp, FILE *yp);
struct message *smime_decrypt_assemble(struct message *m, FILE *hp, FILE *bp);
int ccertsave(void *v);
enum okay rfc2595_hostname_match(const char *host, const char *pattern);
#else
# define ccertsave	ccmdnotsupp
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

void *		salloc(size_t size);
void *		csalloc(size_t nmemb, size_t size);
void		sreset(void);
void		spreserve(void);
#ifdef HAVE_ASSERTS
int		sstats(void *v);
#endif
char *		savestr(char const *str);
char *		savestrbuf(char const *sbuf, size_t sbuf_len);
char *		save2str(char const *str, char const *old);
char *		savecat(char const *s1, char const *s2);

/* Create duplicate, lowercasing all characters along the way */
char *		i_strdup(char const *src);

/* Extract the protocol base and return a duplicate */
char *		protbase(char const *cp);

/* URL en- and decoding (RFC 1738, but not really) */
char *		urlxenc(char const *cp);
char *		urlxdec(char const *cp);

struct str *	str_concat_csvl(struct str *self, ...);
struct str *	str_concat_cpa(struct str *self, char const *const*cpa,
			char const *sep_o_null);

/* Plain char* support, not auto-reclaimed (unless noted) */

/* Hash the passed string; uses Chris Torek's hash algorithm */
ui_it		strhash(char const *name);

#define hash(S)	(strhash(S) % HSHSIZE) /* xxx COMPAT (?) */

/* Are any of the characters in the two strings the same? */
int		anyof(char const *s1, char const *s2);

/* Treat **iolist* as a comma separated list of strings; find and return the
 * next entry, trimming surrounding whitespace, and point **iolist* to the next
 * entry or to NULL if no more entries are contained.  If *ignore_empty* is not
 * set empty entries are started over.  Return NULL or an entry */
char *		strcomma(char **iolist, int ignore_empty);

/* Copy a string, lowercasing it as we go; *size* is buffer size of *dest*;
 * *dest* will always be terminated unless *size* is 0 */
void		i_strcpy(char *dest, char const *src, size_t size);

/* Is *as1* a valid prefix of *as2*? */
int		is_prefix(char const *as1, char const *as2);

/* Find the last AT @ before the first slash */
char const *	last_at_before_slash(char const *sp);

/* Convert a string to lowercase, in-place and with multibyte-aware */
void		makelow(char *cp);

/* Is *sub* a substring of *str*, case-insensitive and multibyte-aware? */
int		substr(const char *str, const char *sub);

/* Lazy vsprintf wrapper */
#ifndef HAVE_SNPRINTF
int		snprintf(char *str, size_t size, const char *format, ...);
#endif

char *		sstpcpy(char *dst, const char *src);
char *		sstrdup(char const *cp SMALLOC_DEBUG_ARGS);
char *		sbufdup(char const *cp, size_t len SMALLOC_DEBUG_ARGS);
#ifdef HAVE_ASSERTS
# define sstrdup(CP)	sstrdup(CP, __FILE__, __LINE__)
# define sbufdup(CP,L)	sbufdup(CP, L, __FILE__, __LINE__)
#endif

/* Locale-independent character class functions */
int		asccasecmp(char const *s1, char const *s2);
int		ascncasecmp(char const *s1, char const *s2, size_t sz);
char const *	asccasestr(char const *haystack, char const *xneedle);

/* struct str related support funs */

/* *self->s* is srealloc()ed */
struct str *	n_str_dup(struct str *self, struct str const *t
			SMALLOC_DEBUG_ARGS);

/* *self->s* is srealloc()ed, *self->l* incremented */
struct str *	n_str_add_buf(struct str *self, char const *buf, size_t buflen
			SMALLOC_DEBUG_ARGS);
#define n_str_add(S, T)		n_str_add_buf(S, (T)->s, (T)->l)
#define n_str_add_cp(S, CP)	n_str_add_buf(S, CP, (CP) ? strlen(CP) : 0)

#ifdef HAVE_ASSERTS
# define n_str_dup(S,T)		n_str_dup(S, T, __FILE__, __LINE__)
# define n_str_add_buf(S,B,BL)	n_str_add_buf(S, B, BL, __FILE__, __LINE__)
#endif

/* Our iconv(3) wrappers */

#ifdef HAVE_ICONV
iconv_t		n_iconv_open(char const *tocode, char const *fromcode);
/* If *cd* == *iconvd*, assigns -1 to the latter */
void		n_iconv_close(iconv_t cd);

/* Reset encoding state */
void		n_iconv_reset(iconv_t cd);

/* iconv(3), but return *errno* or 0; *skipilseq* forces step over illegal byte
 * sequences; likewise iconv_str(), but which auto-grows on E2BIG errors; *in*
 * and *in_rest_or_null* may be the same object.
 * Note: EINVAL (incomplete sequence at end of input) is NOT handled, so the
 * replacement character must be added manually if that happens at EOF! */
int		n_iconv_buf(iconv_t cd, char const **inb, size_t *inbleft,
			char **outb, size_t *outbleft, bool_t skipilseq);
int		n_iconv_str(iconv_t icp, struct str *out, struct str const *in,
			struct str *in_rest_or_null, bool_t skipilseq);
#endif

/* thread.c */
int thread(void *vp);
int unthread(void *vp);
struct message *next_in_thread(struct message *mp);
struct message *prev_in_thread(struct message *mp);
struct message *this_in_thread(struct message *mp, long n);
int sort(void *vp);
int ccollapse(void *v);
int cuncollapse(void *v);
void uncollapse1(struct message *m, int always);

/*
 * tty.c
 */

/* Overall interactive terminal life cycle for command line editor library */
#if defined HAVE_EDITLINE || defined HAVE_READLINE
# define TTY_WANTS_SIGWINCH
#endif
void	tty_init(void);
void	tty_destroy(void);

/* Rather for main.c / SIGWINCH interaction only */
void	tty_signal(int sig);

/* Read a line after printing `prompt', if set and non-empty.
 * If `n' is not 0, assumes that `*linebuf' has `n' bytes of default content */
int	tty_readline(char const *prompt, char **linebuf, size_t *linesize,
		size_t n SMALLOC_DEBUG_ARGS);
#ifdef HAVE_ASSERTS
# define tty_readline(A,B,C,D)	tty_readline(A, B, C, D, __FILE__, __LINE__)
#endif

/* Add a line (most likely as returned by tty_readline()) to the history
 * (only added for real if non-empty and doesn't begin with U+0020) */
void	tty_addhist(char const *s);

/* [Yy]es or [Nn]o */
bool_t	yorn(char const *msg);

/* Get a password the expected way, return termios_state.ts_linebuf on
 * success or NULL on error */
char *	getuser(char const *query);

/* Get a password the expected way, return termios_state.ts_linebuf on
 * success or NULL on error.
 * termios_state_reset() (def.h) must be called anyway */
char *	getpassword(char const *query);

/* Get both, user and password in the expected way; simply reuses a value that
 * is set, otherwise calls one of the above.
 * Returns true only if we have a user and a password.
 * *user* will be savestr()ed if neither it nor *pass* have a default value
 * (so that termios_state.ts_linebuf carries only one) */
bool_t	getcredentials(char **user, char **pass);

/*
 * varmac.c
 */

/* Assign a value to a variable */
void	assign(char const *name, char const *value);

int	unset_internal(char const *name);

/* Get the value of an option and return it.
 * Look in the environment if its not available locally */
char *	value(const char *name);
#define boption(V)		(! ! value(V))
#define voption(V)		value(V)

int	cdefine(void *v);
int	define1(const char *name, int account);
int	cundef(void *v);
int	ccall(void *v);
int	callhook(char const *name, int newmail);
int	cdefines(void *v);

int	callaccount(char const *name);
int	listaccounts(FILE *fp);
void	delaccount(char const *name);
