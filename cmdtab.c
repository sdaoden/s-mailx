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
static char sccsid[] = "@(#)cmdtab.c	2.2 (gritter) 10/11/02";
#endif
#endif /* not lint */

#include "def.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Define all of the command names and bindings.
 */

const struct cmd cmdtab[] = {
	{ "next",	next,		NDMLIST,	0,	MMNDEL },
	{ "alias",	group,		M|RAWLIST,	0,	1000 },
	{ "print",	type,		MSGLIST,	0,	MMNDEL },
	{ "type",	type,		MSGLIST,	0,	MMNDEL },
	{ "Type",	Type,		MSGLIST,	0,	MMNDEL },
	{ "Print",	Type,		MSGLIST,	0,	MMNDEL },
	{ "visual",	visual,		I|MSGLIST,	0,	MMNORM },
	{ "top",	top,		MSGLIST,	0,	MMNDEL },
	{ "touch",	stouch,		W|MSGLIST,	0,	MMNDEL },
	{ "preserve",	preserve,	W|MSGLIST,	0,	MMNDEL },
	{ "delete",	delete,		W|P|MSGLIST,	0,	MMNDEL },
	{ "dp",		deltype,	W|MSGLIST,	0,	MMNDEL },
	{ "dt",		deltype,	W|MSGLIST,	0,	MMNDEL },
	{ "undelete",	undeletecmd,	P|MSGLIST,	MDELETED,MMNDEL },
	{ "unset",	unset,		M|RAWLIST,	1,	1000 },
	{ "mail",	sendmail,	R|M|I|STRLIST,	0,	0 },
	{ "Mail",	Sendmail,	R|M|I|STRLIST,	0,	0 },
	{ "mbox",	mboxit,		W|MSGLIST,	0,	0 },
	{ "more",	more,		MSGLIST,	0,	MMNDEL },
	{ "page",	more,		MSGLIST,	0,	MMNDEL },
	{ "More",	More,		MSGLIST,	0,	MMNDEL },
	{ "Page",	More,		MSGLIST,	0,	MMNDEL },
	{ "unread",	unread,		MSGLIST,	0,	MMNDEL },
	{ "Unread",	unread,		MSGLIST,	0,	MMNDEL },
	{ "new",	unread,		MSGLIST,	0,	MMNDEL },
	{ "New",	unread,		MSGLIST,	0,	MMNDEL },
	{ "!",		shell,		I|STRLIST,	0,	0 },
	{ "copy",	copycmd,	M|STRLIST,	0,	0 },
	{ "Copy",	Copycmd,	M|STRLIST,	0,	0 },
	{ "chdir",	schdir,		M|RAWLIST,	0,	1 },
	{ "cd",		schdir,		M|RAWLIST,	0,	1 },
	{ "save",	save,		STRLIST,	0,	0 },
	{ "Save",	Save,		STRLIST,	0,	0 },
	{ "source",	source,		M|RAWLIST,	1,	1 },
	{ "set",	set,		M|RAWLIST,	0,	1000 },
	{ "shell",	dosh,		I|NOLIST,	0,	0 },
	{ "version",	pversion,	M|NOLIST,	0,	0 },
	{ "group",	group,		M|RAWLIST,	0,	1000 },
	{ "ungroup",	ungroup,	M|RAWLIST,	0,	1000 },
	{ "unalias",	ungroup,	M|RAWLIST,	0,	1000 },
	{ "write",	swrite,		STRLIST,	0,	0 },
	{ "from",	from,		MSGLIST,	0,	MMNORM },
	{ "file",	file,		T|M|RAWLIST,	0,	1 },
	{ "followup",	followup,	R|I|MSGLIST,	0,	MMNDEL },
	{ "followupall", followupall,	R|I|MSGLIST,	0,	MMNDEL },
	{ "followupsender", followupsender, R|I|MSGLIST, 0,	MMNDEL },
	{ "folder",	file,		T|M|RAWLIST,	0,	1 },
	{ "folders",	folders,	T|M|NOLIST,	0,	0 },
	{ "?",		help,		M|NOLIST,	0,	0 },
	{ "z",		scroll,		M|STRLIST,	0,	0 },
	{ "headers",	headers,	MSGLIST,	0,	MMNDEL },
	{ "help",	help,		M|NOLIST,	0,	0 },
	{ "=",		pdot,		NOLIST,		0,	0 },
	{ "Reply",	Respond,	R|I|MSGLIST,	0,	MMNDEL },
	{ "Respond",	Respond,	R|I|MSGLIST,	0,	MMNDEL },
	{ "Followup",	Followup,	R|I|MSGLIST,	0,	MMNDEL },
	{ "reply",	respond,	R|I|MSGLIST,	0,	MMNDEL },
	{ "replyall",	respondall,	R|I|MSGLIST,	0,	MMNDEL },
	{ "replysender", respondsender,	R|I|MSGLIST,	0,	MMNDEL },
	{ "respond",	respond,	R|I|MSGLIST,	0,	MMNDEL },
	{ "respondall",	respondall,	R|I|MSGLIST,	0,	MMNDEL },
	{ "respondsender", respondsender, R|I|MSGLIST,	0,	MMNDEL },
	{ "Forward",	Forwardcmd,	R|STRLIST,	0,	MMNDEL },
	{ "forward",	forwardcmd,	R|STRLIST,	0,	MMNDEL },
	{ "edit",	editor,		I|MSGLIST,	0,	MMNORM },
	{ "echo",	echo,		M|RAWLIST,	0,	1000 },
	{ "quit",	quitcmd,	NOLIST,		0,	0 },
	{ "list",	pcmdlist,	M|NOLIST,	0,	0 },
	{ "xit",	rexit,		M|NOLIST,	0,	0 },
	{ "exit",	rexit,		M|NOLIST,	0,	0 },
	{ "pipe",	pipecmd,	STRLIST,	0,	MMNDEL },
	{ "|",		pipecmd,	STRLIST,	0,	MMNDEL },
	{ "Pipe",	Pipecmd,	STRLIST,	0,	MMNDEL },
	{ "size",	messize,	MSGLIST,	0,	MMNDEL },
	{ "hold",	preserve,	W|MSGLIST,	0,	MMNDEL },
	{ "if",		ifcmd,		F|M|RAWLIST,	1,	1 },
	{ "else",	elsecmd,	F|M|RAWLIST,	0,	0 },
	{ "endif",	endifcmd,	F|M|RAWLIST,	0,	0 },
	{ "alternates",	alternates,	M|RAWLIST,	0,	1000 },
	{ "ignore",	igfield,	M|RAWLIST,	0,	1000 },
	{ "discard",	igfield,	M|RAWLIST,	0,	1000 },
	{ "retain",	retfield,	M|RAWLIST,	0,	1000 },
	{ "saveignore",	saveigfield,	M|RAWLIST,	0,	1000 },
	{ "savediscard",saveigfield,	M|RAWLIST,	0,	1000 },
	{ "saveretain",	saveretfield,	M|RAWLIST,	0,	1000 },
	{ "unignore",	unignore,	M|RAWLIST,	0,	1000 },
	{ "unretain",	unretain,	M|RAWLIST,	0,	1000 },
	{ "unsaveignore", unsaveignore,	M|RAWLIST,	0,	1000 },
	{ "unsaveretain", unsaveretain,	M|RAWLIST,	0,	1000 },
	{ "inc",	newmail,	T|NOLIST,	0,	0 },
	{ "newmail",	newmail,	T|NOLIST,	0,	0 },
/*	{ "Header",	Header,		STRLIST,	0,	1000 },	*/
#ifdef	DEBUG_COMMANDS
	{ "core",	core,		M|NOLIST,	0,	0 },
	{ "clobber",	clobber,	M|RAWLIST,	0,	1 },
#endif	/* DEBUG_COMMANDS */
	{ 0,		0,		0,		0,	0 }
};
