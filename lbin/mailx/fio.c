#ident "@(#)fio.c	1.3 'attmail mail(1) command'"
#ident	"@(#)mailx:fio.c	1.13.1.2"
/*	Copyright (c) 1984 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)mailx:fio.c	1.13.1.1"

#include "rcv.h"

/*
 * mailx -- a modified version of a University of California at Berkeley
 *	mail program
 *
 * File I/O.
 */

static int getln();

/*
 * Set up the input pointers while copying the mail file into
 * /tmp.
 */

void
setptr(ibuf)
	FILE *ibuf;
{
	int n, newline = 1;
	int ToldUser = FALSE;
	int ctf = FALSE; 		/* header continuation flag */
	long clen = 0;
	long explicit_clen = 0;
	int hdr = 0;
	register char *cp;
	register int l;
	register long s;
	off_t offset;
	char linebuf[LINESIZE];
	int inhead, newmail, Odot;
	short flag;

	if ( !space ) {
		msgCount = 0;
		offset = 0;
		space = 32;
		newmail = 0;
		message = (struct message *)calloc(space, sizeof(struct message));
		if ( message == NULL ) {
			fprintf(stderr,"calloc: insufficient memory for %d messages\n",space);
			exit(1);
			/* NOTREACHED */
		}
		dot = message;
	} else {
		newmail = 1;
		offset = fsize(otf);
	}
	s = 0L;
	l = 0;
	flag = MUSED|MNEW;

	while ((n = getln(linebuf, sizeof linebuf, ibuf)) > 0) {
		if (!newline) {
			goto putout;
		} else if ((hdr = isheader (linebuf, &ctf)) == FALSE) {
			ctf = FALSE;	/* next line can't be cont. */
		}

		/*
			bug msgCount should be checked
			also, ignore anything that looks like a header line
			after the header is finished!!!   -- adb
		*/
		switch (hdr) {
		case H_FROM:

			if ( (msgCount > 0) && (!newmail) ){
				message[msgCount-1].m_size = s;
				message[msgCount-1].m_clen = 
					(explicit_clen <= 0  || clen < explicit_clen) ?
					clen : explicit_clen;
/*				if ( message[msgCount-1].m_text == TRUE ) { */
					message[msgCount-1].m_lines = l;
/*				} else { */
/*					message[msgCount-1].m_lines = -99; */
			/*	} */
				message[msgCount-1].m_flag = flag;
				flag = MUSED|MNEW;
			}
			if ( msgCount >= space ) {

		/* Limit the speed at which the allocated space grows */

				if ( space < 512 )
					space = space*2;
				else
					space += 512;
				errno = 0;
				Odot = dot - &(message[0]);
				message = (struct message *)realloc(message,space*(sizeof( struct message)));
				if ( message == NULL ) {
					perror("realloc failed");
					fprintf(stderr,"realloc: insufficient memory for %d messages\n",space);
					exit(1);
				}
				dot = &message[Odot];
			}
			message[msgCount].m_block = blockof(offset);
			message[msgCount].m_offset = offsetofaddr(offset);
			message[msgCount].m_text = TRUE;
			newmail = 0;
			msgCount++;
			flag = MUSED|MNEW;
			inhead = 1;
			s = 0L;
			explicit_clen = clen = 0L;
			l = 0;
			ToldUser = FALSE;
			break;
		case H_CLEN:
			if (!inhead) {
				break;
			}
			explicit_clen = atol (strpbrk (linebuf, ":")+1);
			break;

		case H_STATUS:
			if (inhead && ishfield(linebuf, "status")) {
				cp = hcontents(linebuf);
				if (strchr(cp, 'R'))
					flag |= MREAD;
				if (strchr(cp, 'O'))
					flag &= ~MNEW;
			}
			break;
		default:
			break;
		}
		if( !inhead && hdr) hdr = 0;
putout:
		offset += n;
		s += (long)n;
		if( !hdr ) {
			if( inhead ) {
				inhead = 0;
				if( n == 1 && linebuf[0] == '\n')
					clen = -1L;	/* cheat: avoid leading blank */
			}
			clen += (long)n;
		}
		if (msgCount && message[msgCount-1].m_text == TRUE) {
			message[msgCount-1].m_text = istext(linebuf,(long)n);
		}
		if (fwrite(linebuf,1,n,otf) != n) {
			fclose(ibuf);
			fflush(otf);
		} else {
			l++;
		}
		if (ferror(otf)) {
			perror("/tmp");
			exit(1);
		}
		if (msgCount == 0) {
			fclose(ibuf);
			fflush(otf);

		}
		newline = (linebuf[n-1] == '\n');

		/* distribution mailx had unreachable code here -- ignore */
	}

	/*
		last plus 1
	*/

	if (n == 0) {
		fflush(otf);
		if ( msgCount ) {
			message[msgCount-1].m_size = s;
			message[msgCount-1].m_clen = clen;
/*			if ( message[msgCount-1].m_text == TRUE ) {   */
				message[msgCount-1].m_lines = l;
/*			} else {   */
/*				message[msgCount-1].m_lines = -99;  */
/*			}       */
			message[msgCount-1].m_flag = flag;
		}
		flag = MUSED|MNEW;
		fclose(ibuf);
		fflush(otf);
		return;
	}
}

/*  HMF:  Code from fio.c. (getln)                                         */

static int
getln(line, max, f)
	char *line;
	int max;
	FILE	*f;
{
	int	i,ch;
	for (i=0; i < max-1 && (ch=getc(f)) != EOF;)
		if ((line[i++] = (char)ch) == '\n') break;
	line[i] = '\0';
	return(i);
}

/*
 * Drop the passed line onto the passed output buffer.
 * If a write error occurs, return -1, else the count of
 * characters written, including the newline.
 */

putline(obuf, linebuf)
	FILE *obuf;
	char *linebuf;
{
	register int c;

	c = strlen(linebuf);
	fputs(linebuf, obuf);
	putc('\n', obuf);
	if (ferror(obuf))
		return(-1);
	return(c+1);
}

/*
 * Read up a line from the specified input into the line
 * buffer.  Return the number of characters read.  Do not
 * include the newline at the end.
 */

readline(ibuf, linebuf)
	FILE *ibuf;
	char *linebuf;
{
	register char *cp;
	register int c;

	do {
		clearerr(ibuf);
		c = getc(ibuf);
		for (cp = linebuf; c != '\n' && c != EOF; c = getc(ibuf)) {
/*
			if (c == 0) {
				fprintf(stderr, "mailx: NUL changed to @\n");
				c = '@';
			}
*/
			if (cp - linebuf < LINESIZE-2)
				*cp++ = (char)c;
		}
	} while (ferror(ibuf) && ibuf == stdin);
	*cp = 0;
	if (c == EOF && cp == linebuf)
		return(0);
	return(cp - linebuf + 1);
}

/*
 * Return a file buffer all ready to read up the
 * passed message pointer.
 */

FILE *
setinput(mp)
	register struct message *mp;
{
	off_t off;

	fflush(otf);
	off = mp->m_block;
	off <<= 9;
	off += mp->m_offset;
	if (fseek(itf, off, 0) < 0) {
		perror("fseek");
		panic("temporary file seek");
	}
	return(itf);
}


/*
 * Delete a file, but only if the file is a plain file.
 */

removefile(name)
	char name[];
{
	struct stat statb;

	if (stat(name, &statb) < 0)
		return(-1);
	if ((statb.st_mode & S_IFMT) != S_IFREG) {
		errno = EISDIR;
		return(-1);
	}
	return(unlink(name));
}

/*
 * Terminate an editing session by attempting to write out the user's
 * file from the temporary.  Save any new stuff appended to the file.
 */
int
edstop()
{
	register int gotcha, c;
	register struct message *mp;
	FILE *obuf, *ibuf, *tbuf = 0, *readstat;
	FILE *lockopen();					/* adb */
	struct stat statb;
	char tempname[30], *id;

	if (readonly)
		return(0);
	holdsigs();
	if (Tflag != NOSTR) {
		if ((readstat = fopen(Tflag, "w")) == NULL)
			Tflag = NOSTR;
	}
	for (mp = &message[0], gotcha = 0; mp < &message[msgCount]; mp++) {
		if (mp->m_flag & MNEW) {
			mp->m_flag &= ~MNEW;
			mp->m_flag |= MSTATUS;
		}
		if (mp->m_flag & (MODIFY|MDELETED|MSTATUS))
			gotcha++;
		if (Tflag != NOSTR && (mp->m_flag & (MREAD|MDELETED)) != 0) {
			if ((id = hfield("article-id", mp, addone)) != NOSTR)
				fprintf(readstat, "%s\n", id);
		}
	}
	if (Tflag != NOSTR)
		fclose(readstat);
	if (!gotcha || Tflag != NOSTR)
		goto done;
	if ((ibuf = lockopen(editfile, "r+w", 0666, -1, -1)) == NULL) {		/* adb */
		perror(editfile);
		relsesigs();
		longjmp(srbuf, 1);
	}
/*	lock(ibuf, "r+", 1);							   adb */
	if (fstat(fileno(ibuf), &statb) >= 0 && statb.st_size > mailsize) {
		strcpy(tempname, "/tmp/mboxXXXXXX");
		mktemp(tempname);
		if ((obuf = fopen(tempname, "w")) == NULL) {
			perror(tempname);
			lockclose(ibuf);				/* adb */
			relsesigs();
			longjmp(srbuf, 1);
		}
		fseek(ibuf, mailsize, 0);
		while ((c = getc(ibuf)) != EOF)
			putc(c, obuf);
		fclose(obuf);
		if ((tbuf = fopen(tempname, "r")) == NULL) {
			perror(tempname);
			lockclose(ibuf);				/* adb */
			removefile(tempname);
			relsesigs();
			longjmp(srbuf, 1);
		}
		removefile(tempname);
	}
	printf("\"%s\" ", editfile);
	flush();
	if ((obuf = fopen(editfile, "w")) == NULL) {
		perror(editfile);
		lockclose(ibuf);					/* adb */
		if (tbuf)
			fclose(tbuf);
		relsesigs();
		longjmp(srbuf, 1);
	}
	c = 0;
	for (mp = &message[0]; mp < &message[msgCount]; mp++) {
		if ((mp->m_flag & MDELETED) != 0)
			continue;
		c++;
		if (send(mp, obuf, 0) < 0) {
			perror(editfile);
			lockclose(ibuf);				/* adb */
			fclose(obuf);
			if (tbuf)
				fclose(tbuf);
			relsesigs();
			longjmp(srbuf, 1);
		}
	}
	gotcha = (c == 0 && ibuf == NULL);
	if (tbuf != NULL) {
		while ((c = getc(tbuf)) != EOF)
			putc(c, obuf);
		fclose(tbuf);
	}
	fflush(obuf);
	if (ferror(obuf)) {
		perror(editfile);
		lockclose(ibuf);					/* adb */
		fclose(obuf);
		relsesigs();
		longjmp(srbuf, 1);
	}
	if (gotcha) {
		removefile(editfile);
		printf("removed\n");
	}
	else
		printf("complete\n");
	lockclose(ibuf);						/* adb */
	fclose(obuf);
	flush();

done:
	relsesigs();
	return(1);
}

/*
 * Hold signals SIGHUP - SIGQUIT.
 */
void
holdsigs()
{
	sighold(SIGHUP);
	sighold(SIGINT);
	sighold(SIGQUIT);
}

/*
 * Release signals SIGHUP - SIGQUIT
 */
void
relsesigs()
{
	sigrelse(SIGHUP);
	sigrelse(SIGINT);
	sigrelse(SIGQUIT);
}

/*
 * Empty the output buffer.
 * (now a no-op; why was it here?)
 */
void
clrbuf(buf)
	register FILE *buf;
{
/***
	buf = stdout;
	buf->_ptr = buf->_base;
	buf->_cnt = BUFSIZ;
***/	(void) buf;
}


/*
 * Flush the standard output.
 */

void
flush()
{
	fflush(stdout);
	fflush(stderr);
}

/*
 * Determine the size of the file possessed by
 * the passed buffer.
 */

off_t
fsize(iob)
	FILE *iob;
{
	register int f;
	struct stat sbuf;

	f = fileno(iob);
	if (fstat(f, &sbuf) < 0)
		return(0);
	return(sbuf.st_size);
}

/*
 * Take a file name, possibly with shell meta characters
 * in it and expand it by using "sh -c echo filename"
 * Return the file name as a dynamic string.
 */

char *
expand(name)
	char name[];
{
	char xname[BUFSIZ];
	char cmdbuf[BUFSIZ];
	register pid_t pid;
	register int l;
	register char *cp;
	int s, pivec[2];
	struct stat sbuf;
	char *Shell;

	if (debug) fprintf(stderr, "expand(%s)=", name);
	if (name[0] == '+') {
		cp = expand(++name);
		if (*cp != '/' && getfold(cmdbuf) >= 0) {
			sprintf(xname, "%s/%s", cmdbuf, cp);
			cp = savestr(xname);
		}
		if (debug) fprintf(stderr, "%s\n", cp);
		return cp;
	}
	if (!anyof(name, "~{[*?$`'\"\\")) {
		if (debug) fprintf(stderr, "%s\n", name);
		return(name);
	}
	if (pipe(pivec) < 0) {
		perror("pipe");
		return(name);
	}
	sprintf(cmdbuf, "echo %s", name);
	if ((pid = fork()) == 0) {
		sigchild();
		close(pivec[0]);
		close(1);
		dup(pivec[1]);
		close(pivec[1]);
		close(2);
		if ((Shell = value("SHELL")) == NOSTR || *Shell=='\0')
			Shell = SHELL;
		execlp(Shell, Shell, "-c", cmdbuf, (char *)0);
		_exit(1);
	}
	if (pid == (pid_t)-1) {
		perror("fork");
		close(pivec[0]);
		close(pivec[1]);
		return(NOSTR);
	}
	close(pivec[1]);
	l = read(pivec[0], xname, BUFSIZ);
	close(pivec[0]);
	while (wait(&s) != pid);
		;
	s &= 0377;
	if (s != 0 && s != SIGPIPE) {
		fprintf(stderr, "\"Echo\" failed\n");
		goto err;
	}
	if (l < 0) {
		perror("read");
		goto err;
	}
	if (l == 0) {
		fprintf(stderr, "\"%s\": No match\n", name);
		goto err;
	}
	if (l == BUFSIZ) {
		fprintf(stderr, "Buffer overflow expanding \"%s\"\n", name);
		goto err;
	}
	xname[l] = 0;
	for (cp = &xname[l-1]; *cp == '\n' && cp > xname; cp--)
		;
	*++cp = '\0';
	if (any(' ', xname) && stat(xname, &sbuf) < 0) {
		fprintf(stderr, "\"%s\": Ambiguous\n", name);
		goto err;
	}
	if (debug) fprintf(stderr, "%s\n", xname);
	return(savestr(xname));

err:
	printf("\n");
	return(NOSTR);
}

/*
 * Determine the current folder directory name.
 */
getfold(name)
	char *name;
{
	char *folder;

	if ((folder = value("folder")) == NOSTR) {
		strcpy(name, homedir);
		return(0);
	}
	if ((folder = expand(folder)) == NOSTR)
		return(-1);
	if (*folder == '/')
		strcpy(name, folder);
	else
		sprintf(name, "%s/%s", homedir, folder);
	return(0);
}

/*
 * A nicer version of Fdopen, which allows us to fclose
 * without losing the open file.
 */

FILE *
Fdopen(fildes, mode)
	char *mode;
{
	register int f;

	f = dup(fildes);
	if (f < 0) {
		perror("dup");
		return(NULL);
	}
	return(fdopen(f, mode));
}

/*
 * return the filename associated with "s".  This function always
 * returns a non-null string (no error checking is done on the receiving end)
 */
char *
Getf(s)
register char *s;
{
	register char *cp;
	static char defbuf[PATHSIZE];

	if (((cp = value(s)) != 0) && *cp) {
		return expand(cp);
	} else if (strcmp(s, "MBOX")==0) {
		strcpy(defbuf, Getf("HOME"));
		strcat(defbuf, "/");
		strcat(defbuf, "mbox");
		return(defbuf);
	} else if (strcmp(s, "DEAD")==0) {
		strcpy(defbuf, Getf("HOME"));
		strcat(defbuf, "/");
		strcat(defbuf, "dead.letter");
		return(defbuf);
	} else if (strcmp(s, "MAILRC")==0) {
		strcpy(defbuf, Getf("HOME"));
		strcat(defbuf, "/");
		strcat(defbuf, ".mailrc");
		return(defbuf);
	} else if (strcmp(s, "HOME")==0) {
		/* no recursion allowed! */
		return(".");
	}
	return("DEAD");	/* "cannot happen" */
}
