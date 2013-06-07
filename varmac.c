/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Variable and macro handling stuff.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
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

#include "rcv.h"
#include "extern.h"

enum mac_flags {
	MAC_NONE	= 0,
	MAC_ACCOUNT	= 1<<0,

	MAC_TYPE_MASK	= MAC_ACCOUNT
};

struct macro {
	struct macro	*ma_next;
	char		*ma_name;
	struct line	*ma_contents;
	enum mac_flags	ma_flags;
};

struct line {
	struct line	*l_next;
	char		*l_line;
	size_t		l_linesize;
};

static struct macro	*_macros[HSHSIZE];

/* Special cased value string allocation */
static char *		_vcopy(char const *str);
static void		_vfree(char *cp);

/* Check for special housekeeping. */
static bool_t	_check_special_vars(char const *name, bool_t enable,
			char **value);

/* If a variable name begins with a lowercase-character and contains at
 * least one '@', it is converted to all-lowercase. This is necessary
 * for lookups of names based on email addresses.
 *
 * Following the standard, only the part following the last '@' should
 * be lower-cased, but practice has established otherwise here */
static char const *	_canonify(char const *vn);

/* Locate a variable and return its variable node */
static struct var *	_lookup(const char *name, ui_it h, bool_t hisset);

/* Line *cp* consists solely of WS and a } */
static bool_t		_is_closing_angle(char const *cp);

/* Lookup for macros/accounts */
static struct macro *	_malook(const char *name, struct macro *data,
				enum mac_flags macfl);

static int		_maexec(struct macro *mp);

/* User display helpers */
static size_t		_list_macros(FILE *fp, enum mac_flags macfl);
static void		_list_line(FILE *fp, struct line *lp);

static void		_undef1(const char *name, enum mac_flags macfl);
static void		_freelines(struct line *lp);

static char *
_vcopy(char const *str)
{
	char *news;
	size_t len;

	if (*str == '\0')
		news = UNCONST("");
	else {
		len = strlen(str) + 1;
		news = smalloc(len);
		memcpy(news, str, len);
	}
	return news;
}

static void
_vfree(char *cp)
{
	if (*cp)
		free(cp);
}

static bool_t
_check_special_vars(char const *name, bool_t enable, char **value)
{
	/* TODO _check_special_vars --> value cache */
	char *cp = NULL;
	bool_t rv = TRU1;
	int flag = 0;

	if (strcmp(name, "debug") == 0)
		flag = OPT_DEBUG;
	else if (strcmp(name, "header") == 0)
		flag = OPT_N_FLAG, enable = ! enable;
	else if (strcmp(name, "skipemptybody") == 0)
		flag = OPT_E_FLAG;
	else if (strcmp(name, "verbose") == 0)
		flag = OPT_VERBOSE;
	else if (strcmp(name, "folder") == 0) {
		rv = var_folder_updated(*value, &cp);
		if (rv && cp != NULL) {
			_vfree(*value);
			/* It's smalloc()ed, but ensure we don't leak */
			if (*cp == '\0') {
				*value = UNCONST("");
				free(cp);
			} else
				*value = cp;
		}
	}

	if (flag) {
		if (enable)
			options |= flag;
		else
			options &= ~flag;
	}
	return rv;
}

static char const *
_canonify(char const *vn)
{
	if (! upperchar(*vn)) {
		char const *vp;

		for (vp = vn; *vp != '\0' && *vp != '@'; ++vp)
			;
		vn = (*vp == '@') ? i_strdup(vn) : vn;
	}
	return vn;
}

static struct var *
_lookup(const char *name, ui_it h, bool_t hset)
{
	struct var **vap, *lvp, *vp;

	if (! hset)
		h = hash(name);
	vap = variables + h;

	for (lvp = NULL, vp = *vap; vp != NULL; lvp = vp, vp = vp->v_link)
		if (*vp->v_name == *name && strcmp(vp->v_name, name) == 0) {
			/* Relink as head, hope it "sorts on usage" over time */
			if (lvp != NULL) {
				lvp->v_link = vp->v_link;
				vp->v_link = *vap;
				*vap = vp;
			}
			goto jleave;
		}
	vp = NULL;
jleave:
	return vp;
}

static bool_t
_is_closing_angle(char const *cp)
{
	bool_t rv = FAL0;
	while (spacechar(*cp))
		++cp;
	if (*cp++ != '}')
		goto jleave;
	while (spacechar(*cp))
		++cp;
	rv = (*cp == '\0');
jleave:
	return rv;
}

static struct macro *
_malook(const char *name, struct macro *data, enum mac_flags macfl)
{
	ui_it h;
	struct macro *mp;

	macfl &= MAC_TYPE_MASK;
	h = hash(name);

	for (mp = _macros[h]; mp != NULL; mp = mp->ma_next)
		if ((mp->ma_flags & MAC_TYPE_MASK) == macfl &&
				mp->ma_name != NULL &&
				strcmp(mp->ma_name, name) == 0)
			break;

	if (data != NULL && mp == NULL) {
		data->ma_next = _macros[h];
		_macros[h] = data;
	}
	return mp;
}

static int
_maexec(struct macro *mp)
{
	int rv = 0;
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
			*cp++ = (*sp != '\n') ? *sp : ' ';
		while (++sp < smax);
		rv = execute(copy, 0, (size_t)(cp - copy));
		ac_free(copy);
	}
	unset_allow_undefined = FAL0;
	return rv;
}

static size_t
_list_macros(FILE *fp, enum mac_flags macfl)
{
	struct macro *mq;
	char const *typestr;
	ui_it ti, mc;
	struct line *lp;

	macfl &= MAC_TYPE_MASK;
	typestr = (macfl & MAC_ACCOUNT) ? "account" : "define";

	for (ti = mc = 0; ti < HSHSIZE; ++ti)
		for (mq = _macros[ti]; mq; mq = mq->ma_next)
			if ((mq->ma_flags & MAC_TYPE_MASK) == macfl &&
					mq->ma_name != NULL) {
				if (++mc > 1)
					fputc('\n', fp);
				fprintf(fp, "%s %s {\n", typestr, mq->ma_name);
				for (lp = mq->ma_contents; lp; lp = lp->l_next)
					_list_line(fp, lp);
				fputs("}\n", fp);
			}
	return mc;
}

static void
_list_line(FILE *fp, struct line *lp)
{
	char const *sp = lp->l_line, *spmax = sp + lp->l_linesize;
	int c;

	for (; sp < spmax; ++sp) {
		if ((c = *sp & 0xFF) != '\0') {
			if (c == '\n')
				putc('\\', fp);
			putc(c, fp);
		}
	}
	putc('\n', fp);
}

static void
_undef1(const char *name, enum mac_flags macfl)
{
	struct macro *mp;

	if ((mp = _malook(name, NULL, macfl)) != NULL) {
		_freelines(mp->ma_contents);
		free(mp->ma_name);
		mp->ma_name = NULL;
	}
}

static void
_freelines(struct line *lp)
{
	struct line *lq;

	for (lq = NULL; lp != NULL; ) {
		free(lp->l_line);
		if (lq != NULL)
			free(lq);
		lq = lp;
		lp = lp->l_next;
	}
	free(lq);
}

ui_it
hash(char const *name)
{
	ui_it h = 0;

	while (*name != '\0') {
		h *= 33;
		h += *name++;
	}
	return h % HSHSIZE;
}

void
assign(char const *name, char const *value)
{
	struct var *vp;
	ui_it h;
	char *oval;

	if (value == NULL) {
		bool_t save = unset_allow_undefined;
		unset_allow_undefined = TRU1;
		unset_internal(name);
		unset_allow_undefined = save;
		goto jleave;
	}

	name = _canonify(name);
	h = hash(name);

	vp = _lookup(name, h, TRU1);
	if (vp == NULL) {
		vp = (struct var*)scalloc(1, sizeof *vp);
		vp->v_name = _vcopy(name);
		vp->v_link = variables[h];
		variables[h] = vp;
		oval = UNCONST("");
	} else
		oval = vp->v_value;
	vp->v_value = _vcopy(value);

	/* Check if update allowed XXX wasteful on error! */
	if (! _check_special_vars(name, TRU1, &vp->v_value)) {
		char *cp = vp->v_value;
		vp->v_value = oval;
		oval = cp;
	}
	if (*oval != '\0')
		_vfree(oval);
jleave:	;
}

int
unset_internal(char const *name)
{
	int ret = 1;
	ui_it h;
	struct var *vp;

	name = _canonify(name);
	h = hash(name);

	if ((vp = _lookup(name, h, TRU1)) == NULL) {
		if (! sourcing && ! unset_allow_undefined) {
			fprintf(stderr,
				tr(203, "\"%s\": undefined variable\n"), name);
			goto jleave;
		}
	} else {
		/* Always listhead after _lookup() */
		variables[h] = variables[h]->v_link;
		_vfree(vp->v_name);
		_vfree(vp->v_value);
		free(vp);

		_check_special_vars(name, FAL0, NULL);
	}
	ret = 0;
jleave:
	return ret;
}

char *
value(const char *name)
{
	struct var *vp;
	char *vs;

	name = _canonify(name);
	if ((vp = _lookup(name, 0, FAL0)) == NULL) {
		if ((vs = getenv(name)) != NULL && *vs)
			vs = savestr(vs);
		return (vs);
	}
	return (vp->v_value);
}

int
cdefine(void *v)
{
	int rv = 1;
	char **args = v;
	char const *errs;

	if (args[0] == NULL) {
		errs = tr(504, "Missing macro name to `define'");
		goto jerr;
	}
	if (args[1] == NULL || strcmp(args[1], "{") || args[2] != NULL) {
		errs = tr(505, "Syntax is: define <name> {");
		goto jerr;
	}
	rv = define1(args[0], 0);
jleave:
	return rv;
jerr:
	fprintf(stderr, "%s\n", errs);
	goto jleave;
}

int
define1(char const *name, int account) /* TODO make static (`account'...)! */
{
	int ret = 1, n;
	struct macro *mp;
	struct line *lp, *lst = NULL, *lnd = NULL;
	char *linebuf = NULL;
	size_t linesize = 0;

	mp = scalloc(1, sizeof *mp);
	mp->ma_name = sstrdup(name);
	if (account)
		mp->ma_flags |= MAC_ACCOUNT;

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
		if (_is_closing_angle(linebuf))
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
	if (_malook(mp->ma_name, mp, account ? MAC_ACCOUNT : MAC_NONE)
			!= NULL) {
		if (! account) {
			fprintf(stderr,
				tr(76,"A macro named \"%s\" already exists.\n"),
				mp->ma_name);
			lst = mp->ma_contents;
			goto jerr;
		}
		_undef1(mp->ma_name, MAC_ACCOUNT);
		_malook(mp->ma_name, mp, MAC_ACCOUNT);
	}

	ret = 0;
jleave:
	if (linebuf != NULL)
		free(linebuf);
	return ret;
jerr:
	if (lst != NULL)
		_freelines(lst);
	free(mp->ma_name);
	free(mp);
	goto jleave;
}

int
cundef(void *v)
{
	int rv = 1;
	char **args = v;

	if (*args == NULL) {
		fprintf(stderr, tr(506, "Missing macro name to `undef'\n"));
		goto jleave;
	}
	do
		_undef1(*args, MAC_NONE);
	while (*++args);
	rv = 0;
jleave:
	return rv;
}

int
ccall(void *v)
{
	int rv = 1;
	char **args = v;
	char const *errs, *name;
	struct macro *mp;

	if (args[0] == NULL || (args[1] != NULL && args[2] != NULL)) {
		errs = tr(507, "Syntax is: call <%s>\n");
		name = "name";
		goto jerr;
	}
	if ((mp = _malook(*args, NULL, MAC_NONE)) == NULL) {
		errs = tr(508, "Undefined macro called: \"%s\"\n");
		name = *args;
		goto jerr;
	}
	rv = _maexec(mp);
jleave:
	return rv;
jerr:
	fprintf(stderr, errs, name);
	goto jleave;
}

int
callhook(char const *name, int newmail)
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
	if ((mp = _malook(cp, NULL, MAC_NONE)) == NULL) {
		fprintf(stderr, tr(49, "Cannot call hook for folder \"%s\": "
			"Macro \"%s\" does not exist.\n"),
			name, cp);
		r = 1;
		goto jleave;
	}
	inhook = newmail ? 3 : 1;
	r = _maexec(mp);
	inhook = 0;
jleave:
	ac_free(var);
	return r;
}

int
cdefines(void *v)
{
	FILE *fp;
	char *cp;
	(void)v;

	if ((fp = Ftemp(&cp, "Ra", "w+", 0600, 1)) == NULL) {
		perror("tmpfile");
		return 1;
	}
	rm(cp);
	Ftfree(&cp);

	if (_list_macros(fp, MAC_NONE) > 0)
		try_pager(fp);
	Fclose(fp);
	return 0;
}

int
callaccount(char const *name)
{
	struct macro *mp;

	mp = _malook(name, NULL, MAC_ACCOUNT);
	return (mp == NULL) ? CBAD : _maexec(mp);
}

int
listaccounts(FILE *fp)
{
	return (int)_list_macros(fp, MAC_ACCOUNT);
}

void
delaccount(const char *name)
{
	_undef1(name, MAC_ACCOUNT);
}
