/*
 * S-nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 Steffen "Daode" Nurpmeso.
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

/*
 * Mail -- a mail program
 *
 * Handle name lists.
 */

#include "rcv.h"
#include "extern.h"

#include <errno.h>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

static struct name *	tailof(struct name *name);
static struct name *	extract1(char *line, enum gfield ntype,
				char *separators, int copypfx);
static char *		yankword(char *ap, char *wbuf, char *separators,
				int copypfx);
static int		same_name(char *n1, char *n2);
static struct name *	gexpand(struct name *nlist, struct grouphead *gh,
				int metoo, int ntype);
static struct name *	put(struct name *list, struct name *node);
static struct name *	delname(struct name *np, char *name);

/*
 * Allocate a single element of a name list,
 * initialize its name field to the passed
 * name and return it.
 */
struct name *
nalloc(char *str, enum gfield ntype)
{
	struct addrguts ag;
	struct str in, out;
	struct name *np;

	np = (struct name*)salloc(sizeof *np);
	np->n_flink = NULL;
	np->n_blink = NULL;
	np->n_type = ntype;
	np->n_flags = 0;

	(void)addrspec_with_guts((ntype & (GFULL|GSKIN|GREF)) != 0, str, &ag);
	if ((ag.ag_n_flags & NAME_NAME_SALLOC) == 0) {
		ag.ag_n_flags |= NAME_NAME_SALLOC;
		ag.ag_skinned = savestr(ag.ag_skinned);
	}
	np->n_fullname = np->n_name = ag.ag_skinned;
	np->n_flags = ag.ag_n_flags;

	if (ntype & GFULL) {
		if (ag.ag_ilen != ag.ag_slen) {
			in.s = str;
			in.l = ag.ag_ilen;
			mime_fromhdr(&in, &out, TD_ISPR|TD_ICONV);
			np->n_fullname = savestr(out.s);
			free(out.s);
			np->n_flags |= NAME_FULLNAME_SALLOC;
		}
	} else if (ntype & GREF) { /* TODO LEGACY */
		/* TODO Unfortunately we had to skin GREFerences i.e. the
		 * TODO surrounding angle brackets have been stripped away.
		 * TODO Necessarily since otherwise the plain address check
		 * TODO fails due to them; insert them back so that valid
		 * TODO headers will be created */
		np->n_fullname = np->n_name = str = salloc(ag.ag_slen + 2 + 1);
		*(str++) = '<';
		memcpy(str, ag.ag_skinned, ag.ag_slen);
		str += ag.ag_slen;
		*(str++) = '>';
		*str = '\0';
	}
	return (np);
}

struct name *
ndup(struct name *np, enum gfield ntype)
{
	struct name *nnp;

	if ((ntype & (GFULL|GSKIN)) && (np->n_flags & NAME_SKINNED) == 0) {
		nnp = nalloc(np->n_name, ntype);
		goto jleave;
	}

	nnp = (struct name*)salloc(sizeof *np);
	nnp->n_flink = nnp->n_blink = NULL;
	nnp->n_type = ntype;
	nnp->n_flags = (np->n_flags &
			~(NAME_NAME_SALLOC | NAME_FULLNAME_SALLOC)) |
		NAME_NAME_SALLOC;
	nnp->n_name = savestr(np->n_name);
	if (np->n_name == np->n_fullname || (ntype & (GFULL|GSKIN)) == 0)
		nnp->n_fullname = nnp->n_name;
	else {
		nnp->n_flags |= NAME_FULLNAME_SALLOC;
		nnp->n_fullname = savestr(np->n_fullname);
	}
jleave:
	return (nnp);
}

/*
 * Find the tail of a list and return it.
 */
static struct name *
tailof(struct name *name)
{
	struct name *np;

	np = name;
	if (np == NULL)
		return(NULL);
	while (np->n_flink != NULL)
		np = np->n_flink;
	return(np);
}

/*
 * Extract a list of names from a line,
 * and make a list of names from it.
 * Return the list or NULL if none found.
 */
struct name *
extract(char *line, enum gfield ntype)
{
	return extract1(line, ntype, " \t,(", 0);
}

struct name *
sextract(char *line, enum gfield ntype)
{
	if (line && strpbrk(line, ",\"\\(<"))
		return extract1(line, ntype, ",", 1);
	else
		return extract(line, ntype);
}

struct name *
lextract(char *line, enum gfield ntype)
{
	return (extract1(line, ntype, ",", 1));
}

static struct name *
extract1(char *line, enum gfield ntype, char *separators, int copypfx)
{
	char *cp, *nbuf;
	struct name *top, *np, *t;

	if (line == NULL || *line == '\0')
		return NULL;
	top = NULL;
	np = NULL;
	cp = line;
	nbuf = ac_alloc(strlen(line) + 1);
	while ((cp = yankword(cp, nbuf, separators, copypfx)) != NULL) {
		t = nalloc(nbuf, ntype);
		if (top == NULL)
			top = t;
		else
			np->n_flink = t;
		t->n_blink = np;
		np = t;
	}
	ac_free(nbuf);
	return top;
}

/*
 * Turn a list of names into a string of the same names.
 */
char *
detract(struct name *np, enum gfield ntype)
{
	int s;
	char *cp, *top;
	struct name *p;
	int comma;

	comma = ntype & GCOMMA;
	if (np == NULL)
		return(NULL);
	ntype &= ~GCOMMA;
	s = 0;
	if ((debug || value("debug")) && comma)
		fprintf(stderr, catgets(catd, CATSET, 145,
				"detract asked to insert commas\n"));
	for (p = np; p != NULL; p = p->n_flink) {
		if (ntype && (p->n_type & GMASK) != ntype)
			continue;
		s += strlen(p->n_fullname) + 1;
		if (comma)
			s++;
	}
	if (s == 0)
		return(NULL);
	s += 2;
	top = salloc(s);
	cp = top;
	for (p = np; p != NULL; p = p->n_flink) {
		if (ntype && (p->n_type & GMASK) != ntype)
			continue;
		cp = sstpcpy(cp, p->n_fullname);
		if (comma && p->n_flink != NULL)
			*cp++ = ',';
		*cp++ = ' ';
	}
	*--cp = 0;
	if (comma && *--cp == ',')
		*cp = 0;
	return(top);
}

/*
 * Grab a single word (liberal word)
 * Throw away things between ()'s, and take anything between <>.
 * Strip trailing whitespace as *ap* may come directly from user.
 */
static char *
yankword(char *ap, char *wbuf, char *separators, int copypfx)
{
	char *cp, *pp, *wp;

	cp = ap;
	wp = wbuf;
	while (blankspacechar(*cp) || *cp == ',')
		++cp;
	pp = cp;
	if ((cp = (char*)nexttoken((char*)cp)) == NULL)
		return NULL;
	if (copypfx)
		while (pp < cp)
			*wp++ = *pp++;
	if (*cp ==  '<')
		while (*cp && (*wp++ = *cp++) != '>');
	else {
		int incomm = 0;

		while (*cp && (incomm || !strchr(separators, *cp))) {
			if (*cp == '\"') {
				if (cp == ap || *(cp - 1) != '\\') {
					if (incomm)
						incomm--;
					else
						incomm++;
					*wp++ = '\"';
				} else if (cp != ap) {
					*(wp - 1) = '\"';
				}
				cp++;
				continue;
			}
			*wp++ = *cp++;
		}
	}
	while (wp > wbuf && blankspacechar(wp[-1]))
		--wp;
	*wp = '\0';
	return cp;
}

/*
 * Check all addresses in np and delete invalid ones.
 */
struct name *
checkaddrs(struct name *np)
{
	struct name *n;

	for (n = np; n != NULL;) {
		if (is_addr_invalid(n, 1)) {
			if (n->n_blink)
				n->n_blink->n_flink = n->n_flink;
			if (n->n_flink)
				n->n_flink->n_blink = n->n_blink;
			if (n == np)
				np = n->n_flink;
		}
		n = n->n_flink;
	}
	return (np);
}

/*
 * For each recipient in the passed name list with a /
 * in the name, append the message to the end of the named file
 * and remove him from the recipient list.
 *
 * Recipients whose name begins with | are piped through the given
 * program and removed.
 */
struct name *
outof(struct name *names, FILE *fo, struct header *hp)
{
	int pipecnt, xcnt, *fda, i;
	char *shell, *date;
	struct name *np;
	time_t now;
	FILE *fin = NULL, *fout;
	(void)hp;

	/*
	 * Look through all recipients and do a quick return if no file or pipe
	 * addressee is found.
	 */
	fda = NULL; /* Silence cc */
	for (pipecnt = xcnt = 0, np = names; np != NULL; np = np->n_flink)
		switch (np->n_flags & NAME_ADDRSPEC_ISFILEORPIPE) {
		case NAME_ADDRSPEC_ISFILE:
			++xcnt;
			break;
		case NAME_ADDRSPEC_ISPIPE:
			++pipecnt;
			break;
		}
	if (pipecnt == 0 && xcnt == 0)
		goto jleave;

	/*
	 * Otherwise create an array of file descriptors for each found pipe
	 * addressee to get around the dup(2)-shared-file-offset problem, i.e.,
	 * each pipe subprocess needs its very own file descriptor, and we need
	 * to deal with that.
	 * To make our life a bit easier let's just use the auto-reclaimed
	 * string storage.
	 */
	if (pipecnt == 0) {
		fda = NULL;
		shell = NULL;
	} else {
		fda = (int*)salloc(sizeof(int) * pipecnt);
		for (i = 0; i < pipecnt; ++i)
			fda[i] = -1;
		if ((shell = value("SHELL")) == NULL)
			shell = SHELL;
	}

	time(&now);
	date = ctime(&now);

	for (np = names; np != NULL;) {
		if ((np->n_flags & (NAME_ADDRSPEC_ISFILE|NAME_ADDRSPEC_ISPIPE))
				== 0) {
			np = np->n_flink;
			continue;
		}

		/*
		 * See if we have copied the complete message out yet.
		 * If not, do so.
		 */
		if (image < 0) {
			int c;
			char *tempEdit;

			if ((fout = Ftemp(&tempEdit, "Re", "w", 0600, 1))
					== NULL) {
				perror(tr(146, "Creation of temporary image"));
				++senderr;
				goto jcant;
			}
			image = open(tempEdit, O_RDWR);
			if (image >= 0)
				for (i = 0; i < pipecnt; ++i) {
					int fd = open(tempEdit, O_RDONLY);
					if (fd < 0) {
						(void)close(image);
						image = -1;
						pipecnt = i;
						break;
					}
					fda[i] = fd;
					(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
				}
			unlink(tempEdit);
			Ftfree(&tempEdit);
			if (image < 0) {
				perror(tr(147, "Creating descriptor duplicate "
					"of temporary image"));
				++senderr;
				Fclose(fout);
				goto jcant;
			}
			fcntl(image, F_SETFD, FD_CLOEXEC);

			fprintf(fout, "From %s %s", myname, date);
			c = EOF;
			while (i = c, (c = getc(fo)) != EOF)
				putc(c, fout);
			rewind(fo);
			if (i != '\n')
				putc('\n', fout);
			putc('\n', fout);
			fflush(fout);
			if (ferror(fout)) {
				perror(tr(148, "Finalizing write of temporary "
					"image"));
				Fclose(fout);
				goto jcantfout;
			}
			Fclose(fout);

			/* If we have to serve file addressees, open reader */
			if (xcnt != 0 && (fin = Fdopen(image, "r")) == NULL) {
				perror(tr(149, "Failed to open a duplicate of "
					"the temporary image"));
jcantfout:			++senderr;
				(void)close(image);
				image = -1;
				goto jcant;
			}

			/* From now on use xcnt as a counter for pipecnt */
			xcnt = 0;
		}

		/*
		 * Now either copy "image" to the desired file
		 * or give it as the standard input to the desired
		 * program as appropriate.
		 */

		if (np->n_flags & NAME_ADDRSPEC_ISPIPE) {
			int pid;
			sigset_t nset;

			sigemptyset(&nset);
			sigaddset(&nset, SIGHUP);
			sigaddset(&nset, SIGINT);
			sigaddset(&nset, SIGQUIT);
			pid = start_command(shell, &nset,
				fda[xcnt++], -1, "-c", np->n_name + 1, NULL);
			if (pid < 0) {
				fprintf(stderr, tr(281,
					"Message piping to <%s> failed\n"),
					np->n_name);
				++senderr;
				goto jcant;
			}
			free_child(pid);
		} else {
			char *fname = file_expand(np->n_name);
			if ((fout = Zopen(fname, "a", NULL)) == NULL) {
				fprintf(stderr, tr(282,
					"Message writing to <%s> failed: %s\n"),
					fname, strerror(errno));
				++senderr;
				goto jcant;
			}
			rewind(fin);
			while ((i = getc(fin)) != EOF)
				putc(i, fout);
			if (ferror(fout)) {
				fprintf(stderr, tr(282,
					"Message writing to <%s> failed: %s\n"),
					fname, tr(283, "write error"));
				++senderr;
			}
			Fclose(fout);
		}
jcant:
		/*
		 * In days of old we removed the entry from the
		 * the list; now for sake of header expansion
		 * we leave it in and mark it as deleted.
		 */
		np->n_type |= GDEL;
		np = np->n_flink;
		if (image < 0)
			goto jdelall;
	}
jleave:
	if (fin != NULL)
		Fclose(fin);
	for (i = 0; i < pipecnt; ++i)
		(void)close(fda[i]);
	if (image >= 0) {
		close(image);
		image = -1;
	}
	return (names);

jdelall:
	while (np != NULL) {
		if ((np->n_flags & (NAME_ADDRSPEC_ISFILE|NAME_ADDRSPEC_ISPIPE))
				!= 0)
			np->n_type |= GDEL;
		np = np->n_flink;
	}
	goto jleave;
}

static int
same_name(char *n1, char *n2)
{
	int c1, c2;

	if (value("allnet") != NULL) {
		do {
			c1 = (*n1++ & 0377);
			c2 = (*n2++ & 0377);
			c1 = lowerconv(c1);
			c2 = lowerconv(c2);
			if (c1 != c2)
				return 0;
		} while (c1 != '\0' && c2 != '\0' && c1 != '@' && c2 != '@');
		return 1;
	} else
		return asccasecmp(n1, n2) == 0;
}

/*
 * Map all of the aliased users in the invoker's mailrc
 * file and insert them into the list.
 * Changed after all these months of service to recursively
 * expand names (2/14/80).
 */

struct name *
usermap(struct name *names)
{
	struct name *new, *np, *cp;
	struct grouphead *gh;
	int metoo;

	new = NULL;
	np = names;
	metoo = (value("metoo") != NULL);
	while (np != NULL) {
		if (np->n_name[0] == '\\') {
			cp = np->n_flink;
			new = put(new, np);
			np = cp;
			continue;
		}
		gh = findgroup(np->n_name);
		cp = np->n_flink;
		if (gh != NULL)
			new = gexpand(new, gh, metoo, np->n_type);
		else
			new = put(new, np);
		np = cp;
	}
	return(new);
}

/*
 * Recursively expand a group name.  We limit the expansion to some
 * fixed level to keep things from going haywire.
 * Direct recursion is not expanded for convenience.
 */

static struct name *
gexpand(struct name *nlist, struct grouphead *gh, int metoo, int ntype)
{
	struct group *gp;
	struct grouphead *ngh;
	struct name *np;
	static int depth;
	char *cp;

	if (depth > MAXEXP) {
		printf(catgets(catd, CATSET, 150,
			"Expanding alias to depth larger than %d\n"), MAXEXP);
		return(nlist);
	}
	depth++;
	for (gp = gh->g_list; gp != NULL; gp = gp->ge_link) {
		cp = gp->ge_name;
		if (*cp == '\\')
			goto quote;
		if (strcmp(cp, gh->g_name) == 0)
			goto quote;
		if ((ngh = findgroup(cp)) != NULL) {
			nlist = gexpand(nlist, ngh, metoo, ntype);
			continue;
		}
quote:
		np = nalloc(cp, ntype|GFULL);
		/*
		 * At this point should allow to expand
		 * to self if only person in group
		 */
		if (gp == gh->g_list && gp->ge_link == NULL)
			goto skip;
		if (!metoo && same_name(cp, myname))
			np->n_type |= GDEL;
skip:
		nlist = put(nlist, np);
	}
	depth--;
	return(nlist);
}

/*
 * Concatenate the two passed name lists, return the result.
 */
struct name *
cat(struct name *n1, struct name *n2)
{
	struct name *tail;

	if (n1 == NULL)
		return(n2);
	if (n2 == NULL)
		return(n1);
	tail = tailof(n1);
	tail->n_flink = n2;
	n2->n_blink = tail;
	return(n1);
}

/*
 * Unpack the name list onto a vector of strings.
 * Return an error if the name list won't fit.
 */
char **
unpack(struct name *np)
{
	char **ap, **top;
	struct name *n;
	int t, extra, metoo, verbose;

	n = np;
	if ((t = count(n)) == 0)
		panic(catgets(catd, CATSET, 151, "No names to unpack"));
	/*
	 * Compute the number of extra arguments we will need.
	 * We need at least two extra -- one for "mail" and one for
	 * the terminating 0 pointer.  Additional spots may be needed
	 * to pass along -f to the host mailer.
	 */
	extra = 2;
	extra++;
	metoo = value("metoo") != NULL;
	if (metoo)
		extra++;
	verbose = value("verbose") != NULL;
	if (verbose)
		extra++;
	/*LINTED*/
	top = (char **)salloc((t + extra) * sizeof *top);
	ap = top;
	*ap++ = "send-mail";
	*ap++ = "-i";
	if (metoo)
		*ap++ = "-m";
	if (verbose)
		*ap++ = "-v";
	for (; n != NULL; n = n->n_flink)
		if ((n->n_type & GDEL) == 0)
			*ap++ = n->n_name;
	*ap = NULL;
	return(top);
}

/*
 * Remove all of the duplicates from the passed name list by
 * insertion sorting them, then checking for dups.
 * Return the head of the new list.
 */
struct name *
elide(struct name *names)
{
	struct name *np, *t, *newn, *x;

	if (names == NULL)
		return (NULL);
	/* Throw away all deleted nodes (XXX merge with plain sort below?) */
	for (newn = np = NULL; names != NULL; names = names->n_flink)
		if  ((names->n_type & GDEL) == 0) {
			names->n_blink = np;
			if (np)
				np->n_flink = names;
			else
				newn = names;
			np = names;
		}
	if (newn == NULL)
		return (NULL);

	np = newn->n_flink;
	if (np != NULL)
		np->n_blink = NULL;
	newn->n_flink = NULL;

	while (np != NULL) {
		t = newn;
		while (asccasecmp(t->n_name, np->n_name) < 0) {
			if (t->n_flink == NULL)
				break;
			t = t->n_flink;
		}

		/*
		 * If we ran out of t's, put the new entry after
		 * the current value of t.
		 */

		if (asccasecmp(t->n_name, np->n_name) < 0) {
			t->n_flink = np;
			np->n_blink = t;
			t = np;
			np = np->n_flink;
			t->n_flink = NULL;
			continue;
		}

		/*
		 * Otherwise, put the new entry in front of the
		 * current t.  If at the front of the list,
		 * the new guy becomes the new head of the list.
		 */

		if (t == newn) {
			t = np;
			np = np->n_flink;
			t->n_flink = newn;
			newn->n_blink = t;
			t->n_blink = NULL;
			newn = t;
			continue;
		}

		/*
		 * The normal case -- we are inserting into the
		 * middle of the list.
		 */

		x = np;
		np = np->n_flink;
		x->n_flink = t;
		x->n_blink = t->n_blink;
		t->n_blink->n_flink = x;
		t->n_blink = x;
	}

	/*
	 * Now the list headed up by new is sorted.
	 * Go through it and remove duplicates.
	 */

	np = newn;
	while (np != NULL) {
		t = np;
		while (t->n_flink != NULL &&
		       asccasecmp(np->n_name, t->n_flink->n_name) == 0)
			t = t->n_flink;
		if (t == np || t == NULL) {
			np = np->n_flink;
			continue;
		}
		
		/*
		 * Now t points to the last entry with the same name
		 * as np.  Make np point beyond t.
		 */

		np->n_flink = t->n_flink;
		if (t->n_flink != NULL)
			t->n_flink->n_blink = np;
		np = np->n_flink;
	}
	return (newn);
}

/*
 * Put another node onto a list of names and return
 * the list.
 */
static struct name *
put(struct name *list, struct name *node)
{
	node->n_flink = list;
	node->n_blink = NULL;
	if (list != NULL)
		list->n_blink = node;
	return(node);
}

/*
 * Determine the number of undeleted elements in
 * a name list and return it.
 */
int 
count(struct name *np)
{
	int c;

	for (c = 0; np != NULL; np = np->n_flink)
		if ((np->n_type & GDEL) == 0)
			c++;
	return c;
}

/*
 * Delete the given name from a namelist.
 */
static struct name *
delname(struct name *np, char *name)
{
	struct name *p;

	for (p = np; p != NULL; p = p->n_flink)
		if (same_name(p->n_name, name)) {
			if (p->n_blink == NULL) {
				if (p->n_flink != NULL)
					p->n_flink->n_blink = NULL;
				np = p->n_flink;
				continue;
			}
			if (p->n_flink == NULL) {
				if (p->n_blink != NULL)
					p->n_blink->n_flink = NULL;
				continue;
			}
			p->n_blink->n_flink = p->n_flink;
			p->n_flink->n_blink = p->n_blink;
		}
	return np;
}

/*
 * Pretty print a name list
 * Uncomment it if you need it.
 */

/*
void
prettyprint(struct name *name)
{
	struct name *np;

	np = name;
	while (np != NULL) {
		fprintf(stderr, "%s(%d) ", np->n_name, np->n_type);
		np = np->n_flink;
	}
	fprintf(stderr, "\n");
}
*/

struct name *
delete_alternates(struct name *np)
{
	struct name	*xp;
	char	**ap;

	np = delname(np, myname);
	if (altnames)
		for (ap = altnames; *ap; ap++)
			np = delname(np, *ap);
	if ((xp = sextract(value("from"), GEXTRA|GSKIN)) != NULL)
		while (xp) {
			np = delname(np, xp->n_name);
			xp = xp->n_flink;
		}
	if ((xp = sextract(value("replyto"), GEXTRA|GSKIN)) != NULL)
		while (xp) {
			np = delname(np, xp->n_name);
			xp = xp->n_flink;
		}
	if ((xp = sextract(value("sender"), GEXTRA|GSKIN)) != NULL)
		while (xp) {
			np = delname(np, xp->n_name);
			xp = xp->n_flink;
		}
	return np;
}

int
is_myname(char *name)
{
	struct name	*xp;
	char	**ap;

	if (same_name(myname, name))
		return 1;
	if (altnames)
		for (ap = altnames; *ap; ap++)
			if (same_name(*ap, name))
				return 1;
	if ((xp = sextract(value("from"), GEXTRA|GSKIN)) != NULL)
		while (xp) {
			if (same_name(xp->n_name, name))
				return 1;
			xp = xp->n_flink;
		}
	if ((xp = sextract(value("replyto"), GEXTRA|GSKIN)) != NULL)
		while (xp) {
			if (same_name(xp->n_name, name))
				return 1;
			xp = xp->n_flink;
		}
	if ((xp = sextract(value("sender"), GEXTRA|GSKIN)) != NULL)
		while (xp) {
			if (same_name(xp->n_name, name))
				return 1;
			xp = xp->n_flink;
		}
	return 0;
}
