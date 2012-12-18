/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 Steffen "Daode" Nurpmeso.
 * All rights reserved.
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

/* aux.c */
void panic(const char *format, ...);
void holdint(void);
void relseint(void);
void touch(struct message *mp);
int is_dir(char *name);
int argcount(char **argv);
void i_strcpy(char *dest, const char *src, int size);
char *i_strdup(const char *src);
void makelow(char *cp);
int substr(const char *str, const char *sub);
char *colalign(const char *cp, int col, int fill);
void try_pager(FILE *fp);
int source(void *v);
int unstack(void);
void alter(char *name);
int blankline(char *linebuf);
int anyof(char const *s1, char const *s2);
int is_prefix(const char *as1, const char *as2);
char *last_at_before_slash(const char *sp);
enum protocol which_protocol(const char *name);
const char *protfile(const char *xcp);
char *protbase(const char *cp);
#ifdef USE_IMAP
int disconnected(const char *file);
#endif
unsigned pjw(const char *cp);
long nextprime(long n);
char *strenc(const char *cp);
char *strdec(const char *cp);
#ifdef USE_MD5
/* Returns salloc()ed buffer */
char *md5tohex(void const *vp);
/* Returns salloc()ed buffer */
char *cram_md5_string(char const *user, char const *pass, char const *b64);
#endif
char *getuser(void);
char *getpassword(struct termios *otio, int *reset_tio, const char *query);

/* Discover user login name */
char *	username(void);
/* Return our hostname */
char *	nodename(int mayoverride);
void	transflags(struct message *omessage, long omsgCount, int transparent);

/* Returns salloc()ed buffer */
char *getrandstring(size_t length);

char *sstpcpy(char *dst, const char *src);
char *sstrdup(const char *cp);
enum okay makedir(const char *name);
enum okay cwget(struct cw *cw);
enum okay cwret(struct cw *cw);
void cwrelse(struct cw *cw);
void makeprint(struct str *in, struct str *out);
char *prstr(const char *s);
int prout(const char *s, size_t sz, FILE *fp);
int putuc(int u, int c, FILE *fp);
int asccasecmp(const char *s1, const char *s2);
int ascncasecmp(const char *s1, const char *s2, size_t sz);
char *asccasestr(const char *haystack, const char *xneedle);

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

/*
 * base64.c
 * Base64 encoding as defined in section 6.8 of RFC 2045.
 */

/* How much output is necessary to encode *len* bytes in Base64.
 * This assumes B64_CRLF|B64_MULTILINE is set and that the result is to be
 * terminated */
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

/* Trim WS and make *work* point to the decodable range of *in*.
 * Return the amount of bytes a b64_decode operation on that buffer requires */
size_t		b64_decode_prepare(struct str *work, struct str const *in);

/* If *rest* is set then decoding will assume text input (strip CRs from CRLF
 * sequences, only create output when complete lines have been read),
 * otherwise binary input is assumed and each round will produce output.
 * The buffers of *out* and possibly *rest* will be managed via srealloc().
 * If *len* was 0 on input, b64_decode_prepare() will be called to init it and
 * "adjust *in*", but otherwise it is assumed that this yet happened.
 * Returns OKAY or STOP on error (in which case *out* * is set to an error
 * message); caller is responsible to free buffers.
 */
int		b64_decode(struct str *out, struct str const *in, size_t len,
			struct str *rest);

/* b64_decode() always resets the length of *out* on entry, it doesn't append.
 * Call this to join any *rest* onto *out*. (if non-empty).
 * Simply swaps buffers if possible; frees dangling *rest*; returns *out* */
struct str *	b64_decode_join(struct str *out, struct str *rest);

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
char *get_pager(void);
int headers(void *v);
int scroll(void *v);
int Scroll(void *v);
int screensize(void);
int from(void *v);
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
int set(void *v);
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
struct attachment *edit_attachments(struct attachment *attach);
struct attachment *add_attachment(struct attachment *attach, char *file,
		int expand_file);
FILE *collect(struct header *hp, int printheaders, struct message *mp,
		char *quotefile, int doprefix, int tflag);

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

/* fio.c */
void setptr(FILE *ibuf, off_t offset);
int putline(FILE *obuf, char *linebuf, size_t count);
#define	readline(a, b, c)	readline_restart(a, b, c, 0)
int readline_restart(FILE *ibuf, char **linebuf, size_t *linesize, size_t n);
FILE *setinput(struct mailbox *mp, struct message *m, enum needspec need);
struct message *setdot(struct message *mp);
int rm(char *name);
void holdsigs(void);
void relsesigs(void);
off_t fsize(FILE *iob);
char *file_expand(char const *name);
char *expand(char const *name);
/* Locate the user's mailbox file (where new, unread mail is queued) */
void	findmail(char *user, int force, char *buf, int size);
int getfold(char *name, int size);
char *getdeadletter(void);
char *fgetline(char **line, size_t *linesize, size_t *count,
		size_t *llen, FILE *fp, int appendnl);
void newline_appended(void);
enum okay get_body(struct message *mp);
int sclose(struct sock *sp);
enum okay swrite(struct sock *sp, const char *data);
enum okay swrite1(struct sock *sp, const char *data, int sz, int use_buffer);
int sgetline(char **line, size_t *linesize, size_t *linelen, struct sock *sp);
enum okay sopen(const char *xserver, struct sock *sp, int use_ssl,
		const char *uhp, const char *portstr, int verbose);

/* getname.c */
char *getname(int uid);
int getuserid(char *name);

/* getopt.c */
#ifndef HAVE_GETOPT
int getopt(int argc, char *const argv[], const char *optstring);
#endif

/* head.c */

/* Return the user's From: address(es) */
char const *	myaddrs(struct header *hp);
/* Boil the user's From: addresses down to a single one, or use *sender* */
char const *	myorigin(struct header *hp);

int	is_head(char const *linebuf, size_t linelen);
int	extract_date_from_from_(char const *line, size_t linelen,
		char datebuf[FROM_DATEBUF]);
void extract_header(FILE *fp, struct header *hp);
#define	hfieldX(a, b)	hfield_mult(a, b, 1)
#define	hfield1(a, b)	hfield_mult(a, b, 0)
char *hfield_mult(char *field, struct message *mp, int mult);
char *thisfield(const char *linebuf, const char *field);
char *nameof(struct message *mp, int reptype);
char const *skip_comment(char const *cp);
char *routeaddr(const char *name);
#define is_fileorpipe_addr(NP) \
	(((NP)->n_flags & NAME_ADDRSPEC_ISFILEORPIPE) != 0)
int is_addr_invalid(struct name *np, int putmsg);
char *skinned_name(struct name const*np);
char *skin(char const *name);
int addrspec_with_guts(int doskin, char const *name, struct addrguts *agp);
char *realname(char const *name);
char *name1(struct message *mp, int reptype);
int msgidcmp(const char *s1, const char *s2);
int is_ign(char *field, size_t fieldlen, struct ignoretab ignore[2]);
int member(char *realfield, struct ignoretab *table);
char *fakefrom(struct message *mp);
char *fakedate(time_t t);
time_t unixtime(char const *from);
time_t rfctime(char const *date);
time_t combinetime(int year, int month, int day,
		int hour, int minute, int second);
void substdate(struct message *m);
int check_from_and_sender(struct name *fromfield, struct name *senderfield);
char *getsender(struct message *m);

/* imap.c */
#ifdef USE_IMAP
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
int setfile(char *name, int newmail);
int newmailinfo(int omsgCount);
void commands(void);
int execute(char *linebuf, int contxt, size_t linesize);
void setmsize(int sz);
void onintr(int s);
void announce(int printheaders);
int newfileinfo(void);
int getmdot(int newmail);
int pversion(void *v);
void load(char *name);
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

/* macro.c */
int cdefine(void *v);
int define1(const char *name, int account);
int cundef(void *v);
int ccall(void *v);
int callaccount(const char *name);
int callhook(const char *name, int newmail);
int listaccounts(FILE *fp);
int cdefines(void *v);
void delaccount(const char *name);

/* maildir.c */
int maildir_setfile(const char *name, int newmail, int isedit);
void maildir_quit(void);
enum okay maildir_append(const char *name, FILE *fp);
enum okay maildir_remove(const char *name);

/* main.c */
int main(int argc, char *argv[]);

/* mime.c */
char const *gettcharset(void);
char const *need_hdrconv(struct header *hp, enum gfield w);
#ifdef HAVE_ICONV
iconv_t iconv_open_ft(const char *tocode, const char *fromcode);
size_t iconv_ft(iconv_t cd, char **inb, size_t *inbleft,
		char **outb, size_t *outbleft, int tolerant);
#endif
enum mimeenc mime_getenc(char *h);
int mime_getcontent(char *h);
char *mime_getparam(char *param, char *h);
char *mime_getboundary(char *h);
char *mime_filecontent(char *name);
int get_mime_convert(FILE *fp, char **contenttype, char const **charset,
		enum mimeclean *isclean, int dosign);
void mime_fromhdr(struct str const *in, struct str *out, enum tdflags flags);
char *mime_fromaddr(char const *name);
size_t prefixwrite(void *ptr, size_t size, size_t nmemb, FILE *f,
		char *prefix, size_t prefixlen);
ssize_t	 mime_write(void const *ptr, size_t size, FILE *f,
		enum conversion convert, enum tdflags dflags,
		char *prefix, size_t prefixlen, struct str *rest);

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

struct name *	checkaddrs(struct name *np);
struct name *	usermap(struct name *names);
struct name *	elide(struct name *names);
struct name *	delete_alternates(struct name *np);
int		is_myname(char const *name);

struct name *	outof(struct name *names, FILE *fo, struct header *hp);

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

/* popen.c */
sighandler_type safe_signal(int signum, sighandler_type handler);
FILE *safe_fopen(const char *file, const char *mode, int *omode);
FILE *Fopen(const char *file, const char *mode);
FILE *Fdopen(int fd, const char *mode);
int Fclose(FILE *fp);
FILE *Zopen(const char *file, const char *mode, int *compression);
FILE *Popen(const char *cmd, const char *mode, const char *shell, int newfd1);
int Pclose(FILE *ptr);
void close_all_files(void);
int run_command(char *cmd, sigset_t *mask, int infd, int outfd,
		char *a0, char *a1, char *a2);
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
char *foldergets(char **s, size_t *size, size_t *count, size_t *llen,
		FILE *stream);
#undef send
#define send(a, b, c, d, e, f)  xsend(a, b, c, d, e, f)
int send(struct message *mp, FILE *obuf, struct ignoretab *doign,
		char *prefix, enum sendaction action, off_t *stats);

/* sendout.c */
char *makeboundary(void);
int mail(struct name *to, struct name *cc, struct name *bcc,
		char *subject, struct attachment *attach,
		char *quotefile, int recipient_record, int tflag, int Eflag);
int sendmail(void *v);
int Sendmail(void *v);
enum okay mail1(struct header *hp, int printheaders, struct message *quote,
		char *quotefile, int recipient_record, int doprefix, int tflag,
		int Eflag);
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
 */
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

struct str *	str_concat_csvl(struct str *self, ...);

/* temp.c */
FILE *Ftemp(char **fn, char *prefix, char *mode, int bits, int register_file);
void Ftfree(char **fn);
void tinit(void);

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

/* tty.c */
int grabh(struct header *hp, enum gfield gflags, int subjfirst);
char *readtty(char *prefix, char *string);
int yorn(char *msg);

/* v7.local.c */
void demail(void);

/* vars.c */
void assign(const char *name, const char *value);
char *vcopy(const char *str);
char *value(const char *name);
struct grouphead *findgroup(char *name);
void printgroup(char *name);
int hash(const char *name);
int unset_internal(const char *name);
void remove_group(const char *name);
