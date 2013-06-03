/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Macros.
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

#include "rcv.h"
#include "extern.h"

#define	MAPRIME		29

struct line {
	struct line	*l_next;
	char	*l_line;
	size_t	l_linesize;
};

struct macro {
	struct macro	*ma_next;
	char	*ma_name;
	struct line	*ma_contents;
	enum maflags {
		MA_NOFLAGS,
		MA_ACCOUNT
	} ma_flags;
};

static struct macro	*macros[MAPRIME];
static struct macro	*accounts[MAPRIME];

#define	mahash(cp)	(pjw(cp) % MAPRIME)
static void undef1(const char *name, struct macro **table);
static int maexec(struct macro *mp);
static int closingangle(const char *cp);
static struct macro *malook(const char *name, struct macro *data,
		struct macro **table);
static void freelines(struct line *lp);
static void list0(FILE *fp, struct line *lp);
static int list1(FILE *fp, struct macro **table);

int 
cdefine(void *v)
{
	char	**args = v;

	if (args[0] == NULL) {
		fprintf(stderr, "Missing macro name to define.\n");
		return 1;
	}
	if (args[1] == NULL || strcmp(args[1], "{") || args[2] != NULL) {
		fprintf(stderr, "Syntax is: define <name> {\n");
		return 1;
	}
	return define1(args[0], 0);
}

int 
define1(const char *name, int account)
{
	int ret = 1, n;
	struct macro *mp;
	struct line *lp, *lst = NULL, *lnd = NULL;
	char *linebuf = NULL;
	size_t linesize = 0;

	mp = scalloc(1, sizeof *mp);
	mp->ma_name = sstrdup(name);
	if (account)
		mp->ma_flags |= MA_ACCOUNT;

	for (;;) {
		n = 0;
		for (;;) {
			n = readline_restart(input, &linebuf, &linesize, n);
			if (n < 0)
				break;
			if (n == 0 || linebuf[n - 1] != '\\')
				break;
			linebuf[n - 1] = '\n';
		}
		if (n < 0) {
			fprintf(stderr,
				tr(75, "Unterminated %s definition: \"%s\".\n"),
				account ? "account" : "macro", mp->ma_name);
			if (sourcing)
				unstack();
			goto jerr;
		}
		if (closingangle(linebuf))
			break;
		lp = scalloc(1, sizeof *lp);
		lp->l_linesize = ++n;
		lp->l_line = smalloc(n); /* TODO rewrite this file */
		memcpy(lp->l_line, linebuf, n);
		assert(lp->l_line[n - 1] == '\0');
		if (lst != NULL && lnd != NULL) {
			lnd->l_next = lp;
			lnd = lp;
		} else
			lst = lnd = lp;
	}

	mp->ma_contents = lst;
	if (malook(mp->ma_name, mp, account ? accounts : macros) != NULL) {
		if (! account) {
			fprintf(stderr,
				tr(76,"A macro named \"%s\" already exists.\n"),
				mp->ma_name);
			lst = mp->ma_contents;
			goto jerr;
		}
		undef1(mp->ma_name, accounts);
		malook(mp->ma_name, mp, accounts);
	}

	ret = 0;
jleave:
	if (linebuf != NULL)
		free(linebuf);
	return (ret);

jerr:	if (lst != NULL)
		freelines(lst);
	free(mp->ma_name);
	free(mp);
	goto jleave;
}

int 
cundef(void *v)
{
	char	**args = v;

	if (*args == NULL) {
		fprintf(stderr, "Missing macro name to undef.\n");
		return 1;
	}
	do
		undef1(*args, macros);
	while (*++args);
	return 0;
}

static void 
undef1(const char *name, struct macro **table)
{
	struct macro	*mp;

	if ((mp = malook(name, NULL, table)) != NULL) {
		freelines(mp->ma_contents);
		free(mp->ma_name);
		mp->ma_name = NULL;
	}
}

int 
ccall(void *v)
{
	char	**args = v;
	struct macro	*mp;

	if (args[0] == NULL || (args[1] != NULL && args[2] != NULL)) {
		fprintf(stderr, "Syntax is: call <name>\n");
		return 1;
	}
	if ((mp = malook(*args, NULL, macros)) == NULL) {
		fprintf(stderr, "Undefined macro called: \"%s\"\n", *args);
		return 1;
	}
	return maexec(mp);
}

int 
callaccount(const char *name)
{
	struct macro	*mp;

	if ((mp = malook(name, NULL, accounts)) == NULL)
		return CBAD;
	return maexec(mp);
}

int 
callhook(const char *name, int newmail)
{
	int len, r;
	struct macro *mp;
	char *var, *cp;

	var = ac_alloc(len = strlen(name) + 13);
	snprintf(var, len, "folder-hook-%s", name);
	if ((cp = value(var)) == NULL && (cp = value("folder-hook")) == NULL) {
		r = 0;
		goto jleave;
	}
	if ((mp = malook(cp, NULL, macros)) == NULL) {
		fprintf(stderr, tr(49, "Cannot call hook for folder \"%s\": "
			"Macro \"%s\" does not exist.\n"),
			name, cp);
		r = 1;
		goto jleave;
	}
	inhook = newmail ? 3 : 1;
	r = maexec(mp);
	inhook = 0;
jleave:
	ac_free(var);
	return r;
}

static int 
maexec(struct macro *mp)
{
	int r = 0;
	struct line *lp;
	char const *sp, *smax;
	char *copy, *cp;

	unset_allow_undefined = TRU1;
	for (lp = mp->ma_contents; lp; lp = lp->l_next) {
		sp = lp->l_line;
		smax = lp->l_line + lp->l_linesize;
		while (sp < smax &&
				(blankchar(*sp) || *sp == '\n' || *sp == '\0'))
			++sp;
		if (sp == smax)
			continue;
		cp = copy = ac_alloc(lp->l_linesize + (lp->l_line - sp));
		do
			*cp++ = *sp != '\n' ? *sp : ' ';
		while (++sp < smax);
		r = execute(copy, 0, (size_t)(cp - copy));
		ac_free(copy);
	}
	unset_allow_undefined = FAL0;
	return (r);
}

static int 
closingangle(const char *cp)
{
	while (spacechar(*cp&0377))
		cp++;
	if (*cp++ != '}')
		return 0;
	while (spacechar(*cp&0377))
		cp++;
	return *cp == '\0';
}

static struct macro *
malook(const char *name, struct macro *data, struct macro **table)
{
	struct macro	*mp;
	unsigned	h;

	mp = table[h = mahash(name)];
	while (mp != NULL) {
		if (mp->ma_name && strcmp(mp->ma_name, name) == 0)
			break;
		mp = mp->ma_next;
	}
	if (data) {
		if (mp != NULL)
			return mp;
		data->ma_next = table[h];
		table[h] = data;
	}
	return mp;
}

static void 
freelines(struct line *lp)
{
	struct line	*lq = NULL;

	while (lp) {
		free(lp->l_line);
		free(lq);
		lq = lp;
		lp = lp->l_next;
	}
	free(lq);
}

int
listaccounts(FILE *fp)
{
	return list1(fp, accounts);
}

static void
list0(FILE *fp, struct line *lp)
{
	const char	*sp;
	int	c;

	for (sp = lp->l_line; sp < &lp->l_line[lp->l_linesize]; sp++) {
		if ((c = *sp&0377) != '\0') {
			if ((c = *sp&0377) == '\n')
				putc('\\', fp);
			putc(c, fp);
		}
	}
	putc('\n', fp);
}

static int
list1(FILE *fp, struct macro **table)
{
	struct macro	**mp, *mq;
	struct line	*lp;
	int	mc = 0;

	for (mp = table; mp < &table[MAPRIME]; mp++)
		for (mq = *mp; mq; mq = mq->ma_next)
			if (mq->ma_name) {
				if (mc++)
					fputc('\n', fp);
				fprintf(fp, "%s %s {\n",
						table == accounts ?
							"account" : "define",
						mq->ma_name);
				for (lp = mq->ma_contents; lp; lp = lp->l_next)
					list0(fp, lp);
				fputs("}\n", fp);
			}
	return mc;
}

/*ARGSUSED*/
int 
cdefines(void *v)
{
	FILE	*fp;
	char	*cp;
	int	mc;
	(void)v;

	if ((fp = Ftemp(&cp, "Ra", "w+", 0600, 1)) == NULL) {
		perror("tmpfile");
		return 1;
	}
	rm(cp);
	Ftfree(&cp);
	mc = list1(fp, macros);
	if (mc)
		try_pager(fp);
	Fclose(fp);
	return 0;
}

void 
delaccount(const char *name)
{
	undef1(name, accounts);
}
