/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012, 2013 Steffen "Daode" Nurpmeso.
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

/*
 * Mail -- a mail program
 *
 * Variable handling stuff.
 */

/* Check for special housekeeping. */
static void	_check_special_vars(char const *name, int enable);

/*
 * If a variable name begins with a lowercase-character and contains at
 * least one '@', it is converted to all-lowercase. This is necessary
 * for lookups of names based on email addresses.
 *
 * Following the standard, only the part following the last '@' should
 * be lower-cased, but practice has established otherwise here.
 */
static char const *	canonify(char const *vn);

static void vfree(char *cp);
static struct var *lookup(const char *name, int h);
static void remove_grouplist(struct grouphead *gh);

static void
_check_special_vars(char const *name, int enable)
{
	int flag = 0;

	if (strcmp(name, "debug") == 0)
		flag = OPT_DEBUG;
	else if (strcmp(name, "verbose") == 0)
		flag = OPT_VERBOSE;

	if (flag) {
		if (enable)
			options |= flag;
		else
			options &= ~flag;
	}
}

static char const *
canonify(char const *vn)
{
	char const *vp;

	if (upperchar(*vn))
		return vn;
	for (vp = vn; *vp && *vp != '@'; vp++)
		;
	return (*vp == '@') ? i_strdup(vn) : vn;
}

void
assign(char const *name, char const *value)
{
	struct var *vp;
	int h;

	if (value == NULL) {
		h = unset_allow_undefined;
		unset_allow_undefined = 1;
		unset_internal(name);
		unset_allow_undefined = h;
		goto jleave;
	}

	name = canonify(name);
	h = hash(name);

	vp = lookup(name, h);
	if (vp == NULL) {
		vp = (struct var*)scalloc(1, sizeof *vp);
		vp->v_name = vcopy(name);
		vp->v_link = variables[h];
		variables[h] = vp;
	}
	else
		vfree(vp->v_value);
	vp->v_value = vcopy(value);

	_check_special_vars(name, 1);
jleave:	;
}

int
unset_internal(char const *name)
{
	int ret = 1, h;
	struct var *vp;

	name = canonify(name);
	h = hash(name);

	if ((vp = lookup(name, h)) == NULL) {
		if (! sourcing && ! unset_allow_undefined) {
			fprintf(stderr,
				tr(203, "\"%s\": undefined variable\n"), name);
			goto jleave;
		}
	} else {
		/* Always listhead after lookup() */
		variables[h] = variables[h]->v_link;
		vfree(vp->v_name);
		vfree(vp->v_value);
		free(vp);

		_check_special_vars(name, 0);
	}
	ret = 0;
jleave:
	return (ret);
}

/*
 * Free up a variable string.  We do not bother to allocate
 * strings whose value is "" since they are expected to be frequent.
 * Thus, we cannot free same!
 */
static void 
vfree(char *cp)
{
	if (*cp)
		free(cp);
}

/*
 * Copy a variable value into permanent (ie, not collected after each
 * command) space.  Do not bother to alloc space for ""
 */

char *
vcopy(const char *str)
{
	char *news;
	unsigned len;

	if (*str == '\0')
		return UNCONST("");
	len = strlen(str) + 1;
	news = smalloc(len);
	memcpy(news, str, (int)len);
	return (news);
}

/*
 * Get the value of a variable and return it.
 * Look in the environment if its not available locally.
 */

char *
value(const char *name)
{
	struct var *vp;
	char *vs;

	name = canonify(name);
	if ((vp = lookup(name, -1)) == NULL) {
		if ((vs = getenv(name)) != NULL && *vs)
			vs = savestr(vs);
		return (vs);
	}
	return (vp->v_value);
}

/*
 * Locate a variable and return its variable node.
 */
static struct var *
lookup(const char *name, int h)
{
	struct var **vap, *lvp, *vp;

	vap = variables + ((h >= 0) ? h : hash(name));

	for (lvp = NULL, vp = *vap; vp != NULL; lvp = vp, vp = vp->v_link)
		if (*vp->v_name == *name && strcmp(vp->v_name, name) == 0) {
			/* Relink as head, hope it "sorts on usage" over time */
			if (lvp != NULL) {
				lvp->v_link = vp->v_link;
				vp->v_link = *vap;
				*vap = vp;
			}
			return (vp);
		}
	return (NULL);
}

/*
 * Locate a group name and return it.
 */

struct grouphead *
findgroup(char *name)
{
	struct grouphead *gh;

	for (gh = groups[hash(name)]; gh != NULL; gh = gh->g_link)
		if (*gh->g_name == *name && strcmp(gh->g_name, name) == 0)
			return(gh);
	return(NULL);
}

/*
 * Print a group out on stdout
 */
void 
printgroup(char *name)
{
	struct grouphead *gh;
	struct group *gp;

	if ((gh = findgroup(name)) == NULL) {
		printf(catgets(catd, CATSET, 202, "\"%s\": not a group\n"),
				name);
		return;
	}
	printf("%s\t", gh->g_name);
	for (gp = gh->g_list; gp != NULL; gp = gp->ge_link)
		printf(" %s", gp->ge_name);
	putchar('\n');
}

/*
 * Hash the passed string and return an index into
 * the variable or group hash table.
 * Use Chris Torek's hash algorithm.
 */
int 
hash(const char *name)
{
	int h = 0;

	while (*name) {
		h *= 33;
		h += *name++;
	}
	if (h < 0 && (h = -h) < 0)
		h = 0;
	return (h % HSHSIZE);
}

static void
remove_grouplist(struct grouphead *gh)
{
	struct group *gp, *gq;

	if ((gp = gh->g_list) != NULL) {
		for (; gp; gp = gq) {
			gq = gp->ge_link;
			vfree(gp->ge_name);
			free(gp);
		}
	}
}

void
remove_group(const char *name)
{
	struct grouphead *gh, *gp = NULL;
	int h = hash(name);

	for (gh = groups[h]; gh != NULL; gh = gh->g_link) {
		if (*gh->g_name == *name && strcmp(gh->g_name, name) == 0) {
			remove_grouplist(gh);
			vfree(gh->g_name);
			if (gp != NULL)
				gp->g_link = gh->g_link;
			else
				groups[h] = NULL;
			free(gh);
			break;
		}
		gp = gh;
	}
}
