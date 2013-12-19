/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Maildir folder support.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2004
 *	Gunnar Ritter.  All rights reserved.
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
 *	This product includes software developed by Gunnar Ritter
 *	and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

#include <dirent.h>

struct mditem {
	struct message	*md_data;
	unsigned	md_hash;
};

static struct mditem	*_maildir_table;
static long		_maildir_prime;
static sigjmp_buf	_maildir_jmp;

/* Do some cleanup in the tmp/ subdir */
static void		_cleantmp(void);

static int maildir_setfile1(const char *name, int nmail, int omsgCount);
static int mdcmp(const void *a, const void *b);
static int subdir(const char *name, const char *sub, int nmail);
static void _maildir_append(const char *name, const char *sub, const char *fn);
static void readin(const char *name, struct message *m);
static void maildir_update(void);
static void move(struct message *m);
static char *mkname(time_t t, enum mflag f, const char *pref);
static void maildircatch(int s);
static enum okay maildir_append1(const char *name, FILE *fp, off_t off1,
		long size, enum mflag flag);
static enum okay trycreate(const char *name);
static enum okay mkmaildir(const char *name);
static struct message *mdlook(const char *name, struct message *data);
static void mktable(void);
static enum okay subdir_remove(const char *name, const char *sub);

static void
_cleantmp(void)
{
	char dep[MAXPATHLEN];
	struct stat st;
	time_t now;
	DIR *dirp;
	struct dirent *dp;

	if ((dirp = opendir("tmp")) == NULL)
		goto jleave;

	time(&now);
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.')
			continue;
		sstpcpy(sstpcpy(dep, "tmp/"), dp->d_name);
		if (stat(dep, &st) < 0)
			continue;
		if (st.st_atime + 36*3600 < now)
			unlink(dep);
	}
	closedir(dirp);
jleave:	;
}

FL int
maildir_setfile(char const * volatile name, int nmail, int isedit)
{
	sighandler_type	volatile saveint;
	struct cw	cw;
	int	i = -1, omsgCount;

	(void)&saveint;
	(void)&i;
	omsgCount = msgCount;
	if (cwget(&cw) == STOP) {
		fprintf(stderr, "Fatal: Cannot open current directory\n");
		return -1;
	}
	if (!nmail)
		quit();
	saveint = safe_signal(SIGINT, SIG_IGN);
	if (chdir(name) < 0) {
		fprintf(stderr, "Cannot change directory to \"%s\".\n", name);
		cwrelse(&cw);
		return -1;
	}
	if (!nmail) {
		edit = (isedit != 0);
		if (mb.mb_itf) {
			fclose(mb.mb_itf);
			mb.mb_itf = NULL;
		}
		if (mb.mb_otf) {
			fclose(mb.mb_otf);
			mb.mb_otf = NULL;
		}
		initbox(name);
		mb.mb_type = MB_MAILDIR;
	}
	_maildir_table = NULL;
	if (sigsetjmp(_maildir_jmp, 1) == 0) {
		if (nmail)
			mktable();
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, maildircatch);
		i = maildir_setfile1(name, nmail, omsgCount);
	}
	if (nmail && _maildir_table != NULL)
		free(_maildir_table);
	safe_signal(SIGINT, saveint);
	if (i < 0) {
		mb.mb_type = MB_VOID;
		*mailname = '\0';
		msgCount = 0;
	}
	if (cwret(&cw) == STOP) {
		fputs("Fatal: Cannot change back to current directory.\n",
				stderr);
		abort();
	}
	cwrelse(&cw);
	setmsize(msgCount);
	if (nmail && mb.mb_sorted && msgCount > omsgCount) {
		mb.mb_threaded = 0;
		sort((void *)-1);
	}
	if (!nmail)
		sawcom = FAL0;
	if (!nmail && !edit && msgCount == 0) {
		if (mb.mb_type == MB_MAILDIR && value("emptystart") == NULL)
			fprintf(stderr, "No mail at %s\n", name);
		return 1;
	}
	if (nmail && msgCount > omsgCount)
		newmailinfo(omsgCount);
	return 0;
}

static int
maildir_setfile1(const char *name, int nmail, int omsgCount)
{
	int	i;

	if (! nmail)
		_cleantmp();
	mb.mb_perm = (options & OPT_R_FLAG) ? 0 : MB_DELE;
	if ((i = subdir(name, "cur", nmail)) != 0)
		return i;
	if ((i = subdir(name, "new", nmail)) != 0)
		return i;
	_maildir_append(name, NULL, NULL);
	for (i = nmail?omsgCount:0; i < msgCount; i++)
		readin(name, &message[i]);
	if (nmail) {
		if (msgCount > omsgCount)
			qsort(&message[omsgCount],
					msgCount - omsgCount,
					sizeof *message, mdcmp);
	} else {
		if (msgCount)
			qsort(message, msgCount, sizeof *message, mdcmp);
	}
	return msgCount;
}

/*
 * In combination with the names from mkname(), this comparison function
 * ensures that the order of messages in a maildir folder created by mailx
 * remains always the same. In effect, if a mbox folder is transferred to
 * a maildir folder by 'copy *', the order of the messages in mailx will
 * not change.
 */
static int
mdcmp(const void *a, const void *b)
{
	long	i;

	if ((i = ((struct message const*)a)->m_time -
				((struct message const*)b)->m_time) == 0)
		i = strcmp(&((struct message const*)a)->m_maildir_file[4],
				&((struct message const*)b)->m_maildir_file[4]);
	return i;
}

static int
subdir(const char *name, const char *sub, int nmail)
{
	DIR	*dirp;
	struct dirent	*dp;

	if ((dirp = opendir(sub)) == NULL) {
		fprintf(stderr, "Cannot open directory \"%s/%s\".\n",
				name, sub);
		return -1;
	}
	if (access(sub, W_OK) < 0)
		mb.mb_perm = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.' &&
				(dp->d_name[1] == '\0' ||
				 (dp->d_name[1] == '.' &&
				  dp->d_name[2] == '\0')))
			continue;
		if (dp->d_name[0] == '.')
			continue;
		if (!nmail || mdlook(dp->d_name, NULL) == NULL)
			_maildir_append(name, sub, dp->d_name);
	}
	closedir(dirp);
	return 0;
}

static void
_maildir_append(const char *name, const char *sub, const char *fn)
{
	struct message	*m;
	size_t	sz, i;
	time_t	t = 0;
	enum mflag	f = MUSED|MNOFROM|MNEWEST;
	const char	*cp;
	char	*xp;
	(void)name;

	if (fn && sub) {
		if (strcmp(sub, "new") == 0)
			f |= MNEW;
		t = strtol(fn, &xp, 10);
		if ((cp = strrchr(xp, ',')) != NULL &&
				cp > &xp[2] && cp[-1] == '2' && cp[-2] == ':') {
			while (*++cp) {
				switch (*cp) {
				case 'F':
					f |= MFLAGGED;
					break;
				case 'R':
					f |= MANSWERED;
					break;
				case 'S':
					f |= MREAD;
					break;
				case 'T':
					f |= MDELETED;
					break;
				case 'D':
					f |= MDRAFT;
					break;
				}
			}
		}
	}
	if (msgCount + 1 >= msgspace) {
		const int	chunk = 64;
		message = srealloc(message,
				(msgspace += chunk) * sizeof *message);
		memset(&message[msgCount], 0, chunk * sizeof *message);
	}
	if (fn == NULL || sub == NULL)
		return;
	m = &message[msgCount++];
	i = strlen(fn);
	m->m_maildir_file = smalloc((sz = strlen(sub)) + i + 2);
	memcpy(m->m_maildir_file, sub, sz);
	m->m_maildir_file[sz] = '/';
	memcpy(m->m_maildir_file + sz + 1, fn, i + 1);
	m->m_time = t;
	m->m_flag = f;
	m->m_maildir_hash = ~pjw(fn);
	return;
}

static void
readin(const char *name, struct message *m)
{
	char	*buf;
	size_t	bufsize, buflen, cnt;
	long	size = 0, lines = 0;
	off_t	offset;
	FILE	*fp;
	int	emptyline = 0;

	if ((fp = Fopen(m->m_maildir_file, "r")) == NULL) {
		fprintf(stderr, "Cannot read \"%s/%s\" for message %d\n",
				name, m->m_maildir_file,
				(int)(m - &message[0] + 1));
		m->m_flag |= MHIDDEN;
		return;
	}
	buf = smalloc(bufsize = LINESIZE);
	buflen = 0;
	cnt = fsize(fp);
	fseek(mb.mb_otf, 0L, SEEK_END);
	offset = ftell(mb.mb_otf);
	while (fgetline(&buf, &bufsize, &cnt, &buflen, fp, 1) != NULL) {
		/*
		 * Since we simply copy over data without doing any transfer
		 * encoding reclassification/adjustment we *have* to perform
		 * RFC 4155 compliant From_ quoting here
		 */
		if (is_head(buf, buflen)) {
			putc('>', mb.mb_otf);
			++size;
		}
		size += fwrite(buf, 1, buflen, mb.mb_otf);/*XXX err hdling*/
		emptyline = (*buf == '\n');
		++lines;
	}
	if (!emptyline) {
		putc('\n', mb.mb_otf);
		lines++;
		size++;
	}
	Fclose(fp);
	fflush(mb.mb_otf);
	m->m_size = m->m_xsize = size;
	m->m_lines = m->m_xlines = lines;
	m->m_block = mailx_blockof(offset);
	m->m_offset = mailx_offsetof(offset);
	free(buf);
	substdate(m);
}

FL void
maildir_quit(void)
{
	sighandler_type	saveint;
	struct cw	cw;

	(void)&saveint;
	if (cwget(&cw) == STOP) {
		fprintf(stderr, "Fatal: Cannot open current directory\n");
		return;
	}
	saveint = safe_signal(SIGINT, SIG_IGN);
	if (chdir(mailname) < 0) {
		fprintf(stderr, "Cannot change directory to \"%s\".\n",
				mailname);
		cwrelse(&cw);
		return;
	}
	if (sigsetjmp(_maildir_jmp, 1) == 0) {
		if (saveint != SIG_IGN)
			safe_signal(SIGINT, maildircatch);
		maildir_update();
	}
	safe_signal(SIGINT, saveint);
	if (cwret(&cw) == STOP) {
		fputs("Fatal: Cannot change back to current directory.\n",
				stderr);
		abort();
	}
	cwrelse(&cw);
}

static void
maildir_update(void)
{
	struct message	*m;
	int	dodel, c, gotcha = 0, held = 0, modflags = 0;

	if (mb.mb_perm == 0)
		goto free;
	if (!edit) {
		holdbits();
		for (m = &message[0], c = 0; m < &message[msgCount]; m++) {
			if (m->m_flag & MBOX)
				c++;
		}
		if (c > 0)
			if (makembox() == STOP)
				goto bypass;
	}
	for (m = &message[0], gotcha=0, held=0; m < &message[msgCount]; m++) {
		if (edit)
			dodel = m->m_flag & MDELETED;
		else
			dodel = !((m->m_flag&MPRESERVE) ||
					(m->m_flag&MTOUCH) == 0);
		if (dodel) {
			if (unlink(m->m_maildir_file) < 0)
				fprintf(stderr, "Cannot delete file \"%s/%s\" "
						"for message %d.\n",
						mailname, m->m_maildir_file,
						(int)(m - &message[0] + 1));
			else
				gotcha++;
		} else {
			if ((m->m_flag&(MREAD|MSTATUS)) == (MREAD|MSTATUS) ||
					m->m_flag & (MNEW|MBOXED|MSAVED|MSTATUS|
						MFLAG|MUNFLAG|
						MANSWER|MUNANSWER|
						MDRAFT|MUNDRAFT)) {
				move(m);
				modflags++;
			}
			held++;
		}
	}
bypass:
	if ((gotcha || modflags) && edit) {
		printf(tr(168, "\"%s\" "), displayname);
		printf((value("bsdcompat") || value("bsdmsgs"))
			? tr(170, "complete\n") : tr(212, "updated.\n"));
	} else if (held && !edit && mb.mb_perm != 0) {
		if (held == 1)
			printf(tr(155, "Held 1 message in %s\n"), displayname);
		else
			printf(tr(156, "Held %d messages in %s\n"), held,
				displayname);
	}
	fflush(stdout);
free:	for (m = &message[0]; m < &message[msgCount]; m++)
		free(m->m_maildir_file);
}

static void
move(struct message *m)
{
	char	*fn, *new;

	fn = mkname(0, m->m_flag, &m->m_maildir_file[4]);
	new = savecat("cur/", fn);
	if (strcmp(m->m_maildir_file, new) == 0)
		return;
	if (link(m->m_maildir_file, new) < 0) {
		fprintf(stderr, "Cannot link \"%s/%s\" to \"%s/%s\": "
				"message %d not touched.\n",
				mailname, m->m_maildir_file,
				mailname, new,
				(int)(m - &message[0] + 1));
		return;
	}
	if (unlink(m->m_maildir_file) < 0)
		fprintf(stderr, "Cannot unlink \"%s/%s\".\n",
				mailname, m->m_maildir_file);
}

static char *
mkname(time_t t, enum mflag f, const char *pref)
{
	static unsigned long	cnt;
	static pid_t	mypid;
	char	*cp;
	static char	*node;
	int	size, n, i;

	if (pref == NULL) {
		if (mypid == 0)
			mypid = getpid();
		if (node == NULL) {
			cp = nodename(0);
			n = size = 0;
			do {
				if (UICMP(32, n, <, size + 8))
					node = srealloc(node, size += 20);
				switch (*cp) {
				case '/':
					node[n++] = '\\', node[n++] = '0',
					node[n++] = '5', node[n++] = '7';
					break;
				case ':':
					node[n++] = '\\', node[n++] = '0',
					node[n++] = '7', node[n++] = '2';
					break;
				default:
					node[n++] = *cp;
				}
			} while (*cp++);
		}
		size = 60 + strlen(node);
		cp = salloc(size);
		n = snprintf(cp, size, "%lu.%06lu_%06lu.%s:2,",
				(unsigned long)t,
				(unsigned long)mypid, ++cnt, node);
	} else {
		size = (n = strlen(pref)) + 13;
		cp = salloc(size);
		memcpy(cp, pref, n + 1);
		for (i = n; i > 3; i--)
			if (cp[i-1] == ',' && cp[i-2] == '2' &&
					cp[i-3] == ':') {
				n = i;
				break;
			}
		if (i <= 3) {
			memcpy(cp + n, ":2,", 4);
			n += 3;
		}
	}
	if (n < size - 7) {
		if (f & MDRAFTED)
			cp[n++] = 'D';
		if (f & MFLAGGED)
			cp[n++] = 'F';
		if (f & MANSWERED)
			cp[n++] = 'R';
		if (f & MREAD)
			cp[n++] = 'S';
		if (f & MDELETED)
			cp[n++] = 'T';
		cp[n] = '\0';
	}
	return cp;
}

static void
maildircatch(int s)
{
	siglongjmp(_maildir_jmp, s);
}

FL enum okay
maildir_append(const char *name, FILE *fp)
{
	char	*buf, *bp, *lp;
	size_t	bufsize, buflen, cnt;
	off_t	off1 = -1, offs;
	int	inhead = 1;
	int	flag = MNEW|MNEWEST;
	long	size = 0;
	enum okay	ok;

	if (mkmaildir(name) != OKAY)
		return STOP;
	buf = smalloc(bufsize = LINESIZE);
	buflen = 0;
	cnt = fsize(fp);
	offs = ftell(fp);
	do {
		bp = fgetline(&buf, &bufsize, &cnt, &buflen, fp, 1);
		if (bp == NULL || strncmp(buf, "From ", 5) == 0) {
			if (off1 != (off_t)-1) {
				ok = maildir_append1(name, fp, off1,
						size, flag);
				if (ok == STOP)
					return STOP;
				if (fseek(fp, offs+buflen, SEEK_SET) < 0)
					return STOP;
			}
			off1 = offs + buflen;
			size = 0;
			inhead = 1;
			flag = MNEW;
		} else
			size += buflen;
		offs += buflen;
		if (bp && buf[0] == '\n')
			inhead = 0;
		else if (bp && inhead && ascncasecmp(buf, "status", 6) == 0) {
			lp = &buf[6];
			while (whitechar(*lp&0377))
				lp++;
			if (*lp == ':')
				while (*++lp != '\0')
					switch (*lp) {
					case 'R':
						flag |= MREAD;
						break;
					case 'O':
						flag &= ~MNEW;
						break;
					}
		} else if (bp && inhead &&
				ascncasecmp(buf, "x-status", 8) == 0) {
			lp = &buf[8];
			while (whitechar(*lp&0377))
				lp++;
			if (*lp == ':')
				while (*++lp != '\0')
					switch (*lp) {
					case 'F':
						flag |= MFLAGGED;
						break;
					case 'A':
						flag |= MANSWERED;
						break;
					case 'T':
						flag |= MDRAFTED;
						break;
					}
		}
	} while (bp != NULL);
	free(buf);
	return OKAY;
}

static enum okay
maildir_append1(const char *name, FILE *fp, off_t off1, long size,
		enum mflag flag)
{
	int const attempts = 43200;
	char buf[4096], *fn, *tmp, *new;
	struct stat st;
	FILE *op;
	long n, z;
	int i;
	time_t now;

	/* Create a unique temporary file */
	for (i = 0;; sleep(1), ++i) {
		if (i >= attempts) {
			fprintf(stderr, tr(198,
				"Can't create an unique file name in "
				"\"%s/tmp\".\n"), name);
			return STOP;
		}

		time(&now);
		fn = mkname(now, flag, NULL);
		tmp = salloc(n = strlen(name) + strlen(fn) + 6);
		snprintf(tmp, n, "%s/tmp/%s", name, fn);
		if (stat(tmp, &st) >= 0 || errno != ENOENT)
			continue;

		/* Use "wx" for O_EXCL */
		if ((op = Fopen(tmp, "wx")) != NULL)
			break;
	}

	if (fseek(fp, off1, SEEK_SET) < 0)
		goto jtmperr;
	while (size > 0) {
		z = size > (long)sizeof buf ? (long)sizeof buf : size;
		if ((n = fread(buf, 1, z, fp)) != z ||
				(size_t)n != fwrite(buf, 1, n, op)) {
jtmperr:
			fprintf(stderr, "Error writing to \"%s\".\n", tmp);
			Fclose(op);
			unlink(tmp);
			return STOP;
		}
		size -= n;
	}
	Fclose(op);

	new = salloc(n = strlen(name) + strlen(fn) + 6);
	snprintf(new, n, "%s/new/%s", name, fn);
	if (link(tmp, new) < 0) {
		fprintf(stderr, "Cannot link \"%s\" to \"%s\".\n", tmp, new);
		return STOP;
	}
	if (unlink(tmp) < 0)
		fprintf(stderr, "Cannot unlink \"%s\".\n", tmp);
	return OKAY;
}

static enum okay
trycreate(const char *name)
{
	struct stat	st;

	if (stat(name, &st) == 0) {
		if (!S_ISDIR(st.st_mode)) {
			fprintf(stderr, "\"%s\" is not a directory.\n", name);
			return STOP;
		}
	} else if (makedir(name) != OKAY) {
		fprintf(stderr, "Cannot create directory \"%s\".\n", name);
		return STOP;
	} else
		imap_created_mailbox++;
	return OKAY;
}

static enum okay
mkmaildir(const char *name)
{
	char	*np;
	size_t	sz;
	enum okay	ok = STOP;

	if (trycreate(name) == OKAY) {
		np = ac_alloc((sz = strlen(name)) + 5);
		memcpy(np, name, sz);
		memcpy(np + sz, "/tmp", 5);
		if (trycreate(np) == OKAY) {
			strcpy(&np[sz], "/new");
			if (trycreate(np) == OKAY) {
				strcpy(&np[sz], "/cur");
				if (trycreate(np) == OKAY)
					ok = OKAY;
			}
		}
		ac_free(np);
	}
	return ok;
}

static struct message *
mdlook(const char *name, struct message *data)
{
	struct mditem	*md;
	unsigned	c, h, n = 0;

	if (data && data->m_maildir_hash)
		h = ~data->m_maildir_hash;
	else
		h = pjw(name);
	h %= _maildir_prime;
	md = &_maildir_table[c = h];
	while (md->md_data != NULL) {
		if (strcmp(&md->md_data->m_maildir_file[4], name) == 0)
			break;
		c += n&1 ? -((n+1)/2) * ((n+1)/2) : ((n+1)/2) * ((n+1)/2);
		n++;
		while (c >= (unsigned)_maildir_prime)
			c -= (unsigned)_maildir_prime;
		md = &_maildir_table[c];
	}
	if (data != NULL && md->md_data == NULL)
		md->md_data = data;
	return md->md_data ? md->md_data : NULL;
}

static void
mktable(void)
{
	int	i;

	_maildir_prime = nextprime(msgCount);
	_maildir_table = scalloc(_maildir_prime, sizeof *_maildir_table);
	for (i = 0; i < msgCount; i++)
		mdlook(&message[i].m_maildir_file[4], &message[i]);
}

static enum okay
subdir_remove(const char *name, const char *sub)
{
	char	*path;
	int	pathsize, pathend, namelen, sublen, n;
	DIR	*dirp;
	struct dirent	*dp;

	namelen = strlen(name);
	sublen = strlen(sub);
	path = smalloc(pathsize = namelen + sublen + 30);
	memcpy(path, name, namelen);
	path[namelen] = '/';
	memcpy(path + namelen + 1, sub, sublen);
	path[namelen+sublen+1] = '/';
	path[pathend = namelen + sublen + 2] = '\0';
	if ((dirp = opendir(path)) == NULL) {
		perror(path);
		free(path);
		return STOP;
	}
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_name[0] == '.' &&
				(dp->d_name[1] == '\0' ||
				 (dp->d_name[1] == '.' &&
				  dp->d_name[2] == '\0')))
			continue;
		if (dp->d_name[0] == '.')
			continue;
		n = strlen(dp->d_name);
		if (UICMP(32, pathend + n + 1, >, pathsize))
			path = srealloc(path, pathsize = pathend + n + 30);
		memcpy(path + pathend, dp->d_name, n + 1);
		if (unlink(path) < 0) {
			perror(path);
			closedir(dirp);
			free(path);
			return STOP;
		}
	}
	closedir(dirp);
	path[pathend] = '\0';
	if (rmdir(path) < 0) {
		perror(path);
		free(path);
		return STOP;
	}
	free(path);
	return OKAY;
}

FL enum okay
maildir_remove(const char *name)
{
	if (subdir_remove(name, "tmp") == STOP ||
			subdir_remove(name, "new") == STOP ||
			subdir_remove(name, "cur") == STOP)
		return STOP;
	if (rmdir(name) < 0) {
		perror(name);
		return STOP;
	}
	return OKAY;
}
