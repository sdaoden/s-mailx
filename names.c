/*
 * Nail - a mail user agent derived from Berkeley Mail.
 *
 * Copyright (c) 2000-2002 Gunnar Ritter, Freiburg i. Br., Germany.
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

#ifndef lint
#ifdef	DOSCCS
static char sccsid[] = "@(#)names.c	2.7 (gritter) 11/8/02";
#endif
#endif /* not lint */

/*
 * Mail -- a mail program
 *
 * Handle name lists.
 */

#include "rcv.h"
#include "extern.h"

#ifdef	HAVE_STRINGS_H
#include <strings.h>
#endif

static struct name	*tailof __P((struct name *));
static char	*yankword __P((char *, char []));
static struct name	*gexpand __P((struct name *, struct grouphead *,
				int, int));
static struct name	*put __P((struct name *, struct name *));
static struct name	*delname __P((struct name *, char []));

/*
 * Allocate a single element of a name list,
 * initialize its name field to the passed
 * name and return it.
 */
struct name *
nalloc(str, ntype)
	char str[];
	int ntype;
{
	struct name *np;

	/*LINTED*/
	np = (struct name *)salloc(sizeof *np);
	np->n_flink = NIL;
	np->n_blink = NIL;
	np->n_type = ntype;
	np->n_name = savestr(str);
	return(np);
}

/*
 * Find the tail of a list and return it.
 */
static struct name *
tailof(name)
	struct name *name;
{
	struct name *np;

	np = name;
	if (np == NIL)
		return(NIL);
	while (np->n_flink != NIL)
		np = np->n_flink;
	return(np);
}

/*
 * Extract a list of names from a line,
 * and make a list of names from it.
 * Return the list or NIL if none found.
 */
struct name *
extract(line, ntype)
	char line[];
	int ntype;
{
	char *cp, *nbuf;
	struct name *top, *np, *t;

	if (line == NULL || *line == '\0')
		return NIL;
	top = NIL;
	np = NIL;
	cp = line;
	nbuf = ac_alloc(strlen(line) + 1);
	while ((cp = yankword(cp, nbuf)) != NULL) {
		t = nalloc(nbuf, ntype);
		if (top == NIL)
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
detract(np, ntype)
	struct name *np;
	enum gfield ntype;
{
	int s;
	char *cp, *top;
	struct name *p;
	int comma;

	comma = ntype & GCOMMA;
	if (np == NIL)
		return(NULL);
	ntype &= ~GCOMMA;
	s = 0;
	if ((debug || value("debug")) && comma)
		fprintf(stderr, catgets(catd, CATSET, 145,
				"detract asked to insert commas\n"));
	for (p = np; p != NIL; p = p->n_flink) {
		if (ntype && (p->n_type & GMASK) != ntype)
			continue;
		s += strlen(p->n_name) + 1;
		if (comma)
			s++;
	}
	if (s == 0)
		return(NULL);
	s += 2;
	top = salloc(s);
	cp = top;
	for (p = np; p != NIL; p = p->n_flink) {
		if (ntype && (p->n_type & GMASK) != ntype)
			continue;
		cp = sstpcpy(cp, p->n_name);
		if (comma && p->n_flink != NIL)
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
 */
static char *
yankword(ap, wbuf)
	char *ap, wbuf[];
{
	char *cp, *cp2;

	cp = ap;
	if ((cp = nexttoken(cp)) == NULL)
		return NULL;
	if (*cp ==  '<')
		for (cp2 = wbuf; *cp && (*cp2++ = *cp++) != '>';)
			;
	else {
		int incomm = 0;

		for (cp2 = wbuf; *cp && (incomm || !strchr(" \t,(", *cp)); ) {
			if (*cp == '\"') {
				if (cp == ap || *(cp - 1) != '\\') {
					if (incomm)
						incomm--;
					else
						incomm++;
					*cp2++ = '\"';
				} else if (cp != ap) {
					*(cp2 - 1) = '\"';
				}
				cp++;
				continue;
			}
			*cp2++ = *cp++;
		}
	}
	*cp2 = '\0';
	return cp;
}

/*
 * For each recipient in the passed name list with a /
 * in the name, append the message to the end of the named file
 * and remove him from the recipient list.
 *
 * Recipients whose name begins with | are piped through the given
 * program and removed.
 */
/*ARGSUSED 3*/
struct name *
outof(names, fo, hp)
	struct name *names;
	FILE *fo;
	struct header *hp;
{
	int c, lastc;
	struct name *np, *top;
	time_t now;
	char *date, *fname;
	FILE *fout, *fin;
	int ispipe;

	top = names;
	np = names;
	(void) time(&now);
	date = ctime(&now);
	while (np != NIL) {
		if (!is_fileaddr(np->n_name) && np->n_name[0] != '|') {
			np = np->n_flink;
			continue;
		}
		ispipe = np->n_name[0] == '|';
		if (ispipe)
			fname = np->n_name+1;
		else
			fname = expand(np->n_name);

		/*
		 * See if we have copied the complete message out yet.
		 * If not, do so.
		 */

		if (image < 0) {
			char *tempEdit;

			if ((fout = Ftemp(&tempEdit, "Re", "w", 0600, 1))
					== (FILE *)NULL) {
				perror(catgets(catd, CATSET, 146,
						"temporary edit file"));
				senderr++;
				goto cant;
			}
			image = open(tempEdit, O_RDWR);
			(void) unlink(tempEdit);
			Ftfree(&tempEdit);
			if (image < 0) {
				perror(catgets(catd, CATSET, 147,
						"temporary edit file"));
				senderr++;
				(void) Fclose(fout);
				goto cant;
			}
			(void) fcntl(image, F_SETFD, FD_CLOEXEC);
			fprintf(fout, "From %s %s", myname, date);
			c = EOF;
			while (lastc = c, (c = sgetc(fo)) != EOF)
				(void) sputc(c, fout);
			rewind(fo);
			if (lastc != '\n')
				sputc('\n', fout);
			(void) sputc('\n', fout);
			(void) fflush(fout);
			if (ferror(fout))
				perror(catgets(catd, CATSET, 148,
						"temporary edit file"));
			(void) Fclose(fout);
		}

		/*
		 * Now either copy "image" to the desired file
		 * or give it as the standard input to the desired
		 * program as appropriate.
		 */

		if (ispipe) {
			int pid;
			char *shell;
			sigset_t nset;

			/*
			 * XXX
			 * We can't really reuse the same image file,
			 * because multiple piped recipients will
			 * share the same lseek location and trample
			 * on one another.
			 */
			if ((shell = value("SHELL")) == NULL)
				shell = PATH_CSHELL;
			sigemptyset(&nset);
			sigaddset(&nset, SIGHUP);
			sigaddset(&nset, SIGINT);
			sigaddset(&nset, SIGQUIT);
			pid = start_command(shell, &nset,
				image, -1, "-c", fname, NULL);
			if (pid < 0) {
				senderr++;
				goto cant;
			}
			free_child(pid);
		} else {
			int f;
			if ((fout = Fopen(fname, "a")) == (FILE *)NULL) {
				perror(fname);
				senderr++;
				goto cant;
			}
			if ((f = dup(image)) < 0) {
				perror("dup");
				fin = (FILE *)NULL;
			} else
				fin = Fdopen(f, "r");
			if (fin == (FILE *)NULL) {
				fprintf(stderr, catgets(catd, CATSET, 149,
						"Can't reopen image\n"));
				(void) Fclose(fout);
				senderr++;
				goto cant;
			}
			rewind(fin);
			while ((c = sgetc(fin)) != EOF)
				(void) sputc(c, fout);
			if (ferror(fout))
				senderr++, perror(fname);
			(void) Fclose(fout);
			(void) Fclose(fin);
		}
cant:
		/*
		 * In days of old we removed the entry from the
		 * the list; now for sake of header expansion
		 * we leave it in and mark it as deleted.
		 */
		np->n_type |= GDEL;
		np = np->n_flink;
	}
	if (image >= 0) {
		(void) close(image);
		image = -1;
	}
	return(top);
}

/*
 * Determine if the passed address is a local "send to file" address.
 * If any of the network metacharacters precedes any slashes, it can't
 * be a filename.  We cheat with .'s to allow path names like ./...
 */
int
is_fileaddr(name)
	char *name;
{
	char *cp;

	if (*name == '+')
		return 1;
	for (cp = name; *cp; cp++) {
		if (*cp == '!' || *cp == '%' || *cp == '@')
			return 0;
		if (*cp == '/')
			return 1;
	}
	return 0;
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
usermap(names)
	struct name *names;
{
	struct name *new, *np, *cp;
	struct grouphead *gh;
	int metoo;

	new = NIL;
	np = names;
	metoo = (value("metoo") != NULL);
	while (np != NIL) {
		if (np->n_name[0] == '\\') {
			cp = np->n_flink;
			new = put(new, np);
			np = cp;
			continue;
		}
		gh = findgroup(np->n_name);
		cp = np->n_flink;
		if (gh != NOGRP)
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
gexpand(nlist, gh, metoo, ntype)
	struct name *nlist;
	struct grouphead *gh;
	int metoo, ntype;
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
	for (gp = gh->g_list; gp != NOGE; gp = gp->ge_link) {
		cp = gp->ge_name;
		if (*cp == '\\')
			goto quote;
		if (strcmp(cp, gh->g_name) == 0)
			goto quote;
		if ((ngh = findgroup(cp)) != NOGRP) {
			nlist = gexpand(nlist, ngh, metoo, ntype);
			continue;
		}
quote:
		np = nalloc(cp, ntype);
		/*
		 * At this point should allow to expand
		 * to self if only person in group
		 */
		if (gp == gh->g_list && gp->ge_link == NOGE)
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
cat(n1, n2)
	struct name *n1, *n2;
{
	struct name *tail;

	if (n1 == NIL)
		return(n2);
	if (n2 == NIL)
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
unpack(np)
	struct name *np;
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
	for (; n != NIL; n = n->n_flink)
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
elide(names)
	struct name *names;
{
	struct name *np, *t, *new;
	struct name *x;

	if (names == NIL)
		return(NIL);
	new = names;
	np = names;
	np = np->n_flink;
	if (np != NIL)
		np->n_blink = NIL;
	new->n_flink = NIL;
	while (np != NIL) {
		t = new;
		while (asccasecmp(t->n_name, np->n_name) < 0) {
			if (t->n_flink == NIL)
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
			t->n_flink = NIL;
			continue;
		}

		/*
		 * Otherwise, put the new entry in front of the
		 * current t.  If at the front of the list,
		 * the new guy becomes the new head of the list.
		 */

		if (t == new) {
			t = np;
			np = np->n_flink;
			t->n_flink = new;
			new->n_blink = t;
			t->n_blink = NIL;
			new = t;
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

	np = new;
	while (np != NIL) {
		t = np;
		while (t->n_flink != NIL &&
		       asccasecmp(np->n_name, t->n_flink->n_name) == 0)
			t = t->n_flink;
		if (t == np || t == NIL) {
			np = np->n_flink;
			continue;
		}
		
		/*
		 * Now t points to the last entry with the same name
		 * as np.  Make np point beyond t.
		 */

		np->n_flink = t->n_flink;
		if (t->n_flink != NIL)
			t->n_flink->n_blink = np;
		np = np->n_flink;
	}
	return(new);
}

/*
 * Put another node onto a list of names and return
 * the list.
 */
static struct name *
put(list, node)
	struct name *list, *node;
{
	node->n_flink = list;
	node->n_blink = NIL;
	if (list != NIL)
		list->n_blink = node;
	return(node);
}

/*
 * Determine the number of undeleted elements in
 * a name list and return it.
 */
int
count(np)
	struct name *np;
{
	int c;

	for (c = 0; np != NIL; np = np->n_flink)
		if ((np->n_type & GDEL) == 0)
			c++;
	return c;
}

/*
 * Delete the given name from a namelist.
 */
static struct name *
delname(np, name)
	struct name *np;
	char name[];
{
	struct name *p;

	for (p = np; p != NIL; p = p->n_flink)
		if (same_name(p->n_name, name)) {
			if (p->n_blink == NIL) {
				if (p->n_flink != NIL)
					p->n_flink->n_blink = NIL;
				np = p->n_flink;
				continue;
			}
			if (p->n_flink == NIL) {
				if (p->n_blink != NIL)
					p->n_blink->n_flink = NIL;
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
prettyprint(name)
	struct name *name;
{
	struct name *np;

	np = name;
	while (np != NIL) {
		fprintf(stderr, "%s(%d) ", np->n_name, np->n_type);
		np = np->n_flink;
	}
	fprintf(stderr, "\n");
}
*/

struct name *
delete_alternates(struct name *np)
{
	char **ap;
	char *cp;

	np = delname(np, myname);
	if (altnames)
		for (ap = altnames; *ap; ap++)
			np = delname(np, *ap);
	if ((cp = skin(value("from"))) != NULL)
		np = delname(np, cp);
	if ((cp = skin(value("replyto"))) != NULL)
		np = delname(np, cp);
	return np;
}

int
is_myname(char *name)
{
	char **ap, *cp;

	if (same_name(myname, name))
		return 1;
	if (altnames)
		for (ap = altnames; *ap; ap++)
			if (same_name(*ap, name))
				return 1;
	if ((cp = value("from")) != NULL && same_name(skin(cp), name))
		return 1;
	if ((cp = value("replyto")) != NULL && same_name(skin(cp), name))
		return 1;
	return 0;
}
