/*	$Id: extern.h,v 1.9 2000/05/29 00:29:22 gunnar Exp $	*/
/*	OpenBSD: extern.h,v 1.4 1996/06/08 19:48:21 christos Exp 	*/
/*	NetBSD: extern.h,v 1.4 1996/06/08 19:48:21 christos Exp 	*/

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
 *
 *	@(#)extern.h	8.1 (Berkeley) 6/6/93
 *	NetBSD: extern.h,v 1.4 1996/06/08 19:48:21 christos Exp
 *	$Id: extern.h,v 1.9 2000/05/29 00:29:22 gunnar Exp $
 */

struct name;
struct name *cat __P((struct name *, struct name *));
struct name *delname __P((struct name *, char []));
struct name *elide __P((struct name *));
struct name *extract __P((char [], int));
struct grouphead;
struct name *gexpand __P((struct name *, struct grouphead *, int, int));
struct name *nalloc __P((char [], int));
struct header;
struct name *outof __P((struct name *, FILE *, struct header *));
struct name *put __P((struct name *, struct name *));
struct name *tailof __P((struct name *));
struct name *usermap __P((struct name *));
FILE	*safe_fopen __P((char *, char *));
FILE	*Fdopen __P((int, char *));
FILE	*Fopen __P((char *, char *));
FILE	*Popen __P((char *, char *, char *));
FILE	*collect __P((struct header *, int, struct message*, char *));
char	*copy __P((char *, char *));
char	*copyin __P((char *, char **));
char	*detract __P((struct name *, int));
char	*expand __P((char *));
char	*getdeadletter __P((void));
char	*getname __P((int));
struct message;
char	*hfield __P((char [], struct message *));
char	*ishfield __P((char [], char[], char *));
char	*name1 __P((struct message *, int));
char	*nameof __P((struct message *, int));
char	*nextword __P((char *, char *));
char 	*reedit __P((char *));
FILE	*run_editor __P((FILE *, off_t, int, int));
char	*salloc __P((int));
char	*savestr __P((char *));
FILE	*setinput __P((struct message *));
char	*skin __P((char *));
char	*skip_comment __P((char *));
char	*snarf __P((char [], int *));
char	*username __P((void));
char	*value __P((char []));
char	*vcopy __P((char []));
char	*yankword __P((char *, char []));
int	 Fclose __P((FILE *));
int	 More __P((void *));
int	 Pclose __P((FILE *));
int	 Respond __P((void *));
int	 Type __P((void *));
int	 Pipecmd __P((void *));
int	 pipecmd __P((void *));
int	 Forwardcmd __P((void *));
int	 forwardcmd __P((void *));
int	 _Respond __P((int []));
int	 _respond __P((int *));
void	 alter __P((char *));
int	 alternates __P((void *));
void	 announce __P((void));
int	 anyof __P((char *, char *));
int	 append __P((struct message *, FILE *));
int	 argcount __P((char **));
void	 assign __P((char [], char []));
int	 bangexp __P((char *, int));
int	 blankline __P((char []));
void	 brokpipe __P((int));
int	 charcount __P((char *, int));
int	 check __P((int, int));
void	 clob1 __P((int));
int	 clobber __P((void *));
void	 close_all_files __P((void));
int	 cmatch __P((char *, char *));
void	 collhup __P((int));
void	 collint __P((int));
void	 collstop __P((int));
void	 commands __P((void));
int	 copycmd __P((void *));
int	 core __P((void *));
int	 count __P((struct name *));
int	 delete __P((void *));
int	 delm __P((int []));
int	 deltype __P((void *));
void	 demail __P((void));
int	 dosh __P((void *));
int	 dot_lock __P((const char *, int, FILE *, const char *));
void	 dot_unlock __P((const char *));
int	 echo __P((void *));
int	 edit1 __P((int *, int));
int	 editor __P((void *));
void	 edstop __P((void));
int	 elsecmd __P((void *));
int	 endifcmd __P((void *));
int	 evalcol __P((int));
int	 execute __P((char [], int));
int	 exwrite __P((char [], FILE *, int));
void	 fail __P((char [], char []));
int	 file __P((void *));
struct grouphead *
	 findgroup __P((char []));
void	 findmail __P((char *, char *, int));
int	 first __P((int, int));
void	 fmt __P((char *, struct name *, FILE *, int));
int	 folders __P((void *));
int	 forward __P((char [], FILE *, int));
void	 free_child __P((int));
int	 from __P((void *));
off_t	 fsize __P((FILE *));
int	 getfold __P((char *, int));
int	 gethfield __P((FILE *, char [], int, char **));
int	 getmsglist __P((char *, int *, int));
int	 getrawlist __P((char [], char **, int));
int	 getuserid __P((char []));
int	 grabh __P((struct header *, int));
int	 group __P((void *));
void	 hangup __P((int));
int	 hash __P((char *));
void	 hdrstop __P((int));
int	 headers __P((void *));
int	 help __P((void *));
void	 holdsigs __P((void));
int	 ifcmd __P((void *));
int	 igfield __P((void *));
struct ignoretab;
int	 ignore1 __P((char *[], struct ignoretab *, char *));
int	 igshow __P((struct ignoretab *, char *));
void	 intr __P((int));
int	 isdate __P((char []));
int	 isdir __P((char []));
int	 isfileaddr __P((char *));
int	 ishead __P((char []));
int	 isign __P((char *, struct ignoretab []));
int	 isprefix __P((char *, char *));
void	 istrcpy __P((char *, char *, int));
const struct cmd *
	 lex __P((char []));
void	 load __P((char *));
struct var *
	 lookup __P((char []));
int	 mail __P((struct name *,
	    struct name *, struct name *, struct name *,
	   	 char *, struct name *, char *));
void	 mail1 __P((struct header *, int, struct message *, char *));
void	 makemessage __P((FILE *));
void	 mark __P((int));
int	 markall __P((char [], int));
int	 matchsender __P((char *, int));
int	 matchsubj __P((char *, int));
int	 mboxit __P((void *));
int	 member __P((char *, struct ignoretab *));
void	 mesedit __P((FILE *, int));
void	 mespipe __P((FILE *, char []));
int	 messize __P((void *));
int	 metamess __P((int, int));
int	 more __P((void *));
int	 newfileinfo __P((void));
int	 next __P((void *));
int	 null __P((void *));
void	 panic __P((const char *, ...))
#ifdef	__GNUC__
    __attribute__((__format__(__printf__,1,2),__noreturn__));
#else
    ;
#endif
#ifndef	HAVE_SNPRINTF
int	 snprintf __P((char *, size_t, const char *, ...))
#ifdef	__GNUC__
    __attribute__((__format__(__printf__,1,2),__noreturn__));
#else
    ;
#endif
#endif
struct headline;
void	 parse __P((char [], struct headline *, char []));
int	 pcmdlist __P((void *));
int	 pdot __P((void *));
void	 prepare_child __P((sigset_t *, int, int));
int	 preserve __P((void *));
void	 prettyprint __P((struct name *));
void	 printgroup __P((char []));
void	 printhead __P((int));
int	 puthead __P((struct header *, FILE *, int, int));
int	 putline __P((FILE *, char *));
int	 pversion __P((void *));
void	 quit __P((void));
int	 quitcmd __P((void *));
int	 aux_raise __P((int));
int	 readline __P((FILE *, char *, int));
char 	*readtty __P((char *, char *));
void	 register_file __P((FILE *, int, int));
void	 regret __P((int));
void	 relsesigs __P((void));
int	 respond __P((void *));
int	 retfield __P((void *));
int	 rexit __P((void *));
int	 rm __P((char *));
int	 run_command __P((char *, sigset_t *, int, int, char *, char *, char *));
int	 save __P((void *));
int	 save1 __P((char [], int, char *, struct ignoretab *, int));
void	 savedeadletter __P((FILE *));
int	 saveigfield __P((void *));
int	 saveretfield __P((void *));
int	 scan __P((char **));
void	 scaninit __P((void));
int	 schdir __P((void *));
int	 screensize __P((void));
int	 scroll __P((void *));
int	 send_message __P((struct message *, FILE *,
			struct ignoretab *, char *, int));
int	 sendmail __P((void *));
int	 set __P((void *));
int	 setfile __P((char *));
void	 setmsize __P((int));
void	 setptr __P((FILE *));
void	 setscreensize __P((void));
int	 shell __P((void *));
void	 sigchild __P((int));
void	 sort __P((char **));
int	 source __P((void *));
void	 spreserve __P((void));
void	 sreset __P((void));
int	 start_command __P((char *, sigset_t *, int, int, char *, char *, char *));
void	 stop __P((int));
int	 stouch __P((void *));
int	 swrite __P((void *));
void	 tinit __P((void));
int	 top __P((void *));
void	 touch __P((struct message *));
int	 type __P((void *));
int	 undeletecmd __P((void *));
void	 unmark __P((int));
char	**unpack __P((struct name *));
int	 unread __P((void *));
void	 unregister_file __P((FILE *));
int	 unset __P((void *));
int	 unstack __P((void));
void	 vfree __P((char *));
int	 visual __P((void *));
int	 wait_child __P((int));
int	 wait_command __P((int));
int	 writeback __P((FILE *));
void	*smalloc __P((size_t));
char	*itohex __P((unsigned, char *));
size_t	mime_write_tob64 __P((struct str*, FILE*));
void	mime_fromb64 __P((struct str*, struct str*, int, int));
void	mime_fromb64_b __P((struct str*, struct str*, int, int, FILE*));
int	mime_getenc __P((char*));
int	mime_getcontent __P((char*));
char	*mime_filecontent __P((char*));
char	*mime_getparam __P((char*,char*));
char	*mime_getboundary __P((char*));
char	*mime_getfilename __P((char*));
char	*mime_getcharset __P((int));
int	mime_isclean __P((FILE*));
int	mime_save __P((void *));
void	mime_fromhdr __P((struct str*, struct str*, int));
size_t	mime_write __P((void*, size_t, size_t, FILE*, int, int));
int	is_undisplayable __P((unsigned char));
int	mime_check_attach __P((struct name *));
signal_handler_t safe_signal __P((int, signal_handler_t));
int	start_mta __P((struct name *, struct name *, FILE *));
char	*foldergets __P((char *, int, FILE *));
char	* getcmd __P((char *, int *));
int	forward_msg __P((struct message *, struct name *, int));
int	smtp_mta __P((char *, struct name *, FILE *));
char	*hostname __P((void));
