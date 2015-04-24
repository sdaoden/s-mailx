/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ This header is included by ./lex.c and defines the command array content.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 1980, 1993
 * The Regents of the University of California.  All rights reserved.
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
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
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

/* Some shorter aliases to be able to define a command in two lines */
#define MSGLIST      ARG_MSGLIST
#define STRLIST      ARG_STRLIST
#define RAWLIST      ARG_RAWLIST
#define NOLIST       ARG_NOLIST
#define NDMLIST      ARG_NDMLIST
#define ECHOLIST     ARG_ECHOLIST
#define ARG_ARGMASK  ARG_ARGMASK
#define A            ARG_A
#define F            ARG_F
#define H            ARG_H
#define I            ARG_I
#define M            ARG_M
#define P            ARG_P
#define R            ARG_R
#define T            ARG_T
#define V            ARG_V
#define W            ARG_W
#define O            ARG_O

#ifdef HAVE_DOCSTRINGS
# define DS(S)       , S
#else
# define DS(S)
#endif

   /* Note: the first command in here may NOT expand to an unsupported one! */
   { "next", &c_next, (A | NDMLIST), 0, MMNDEL
     DS(N_("Goes to the next message (-list) and prints it")) },
   { "alias", &c_alias, (M | RAWLIST), 0, 1000
     DS(N_("Show all or the specified <alias>(es), or (re)define one")) },
   { "print", &c_type, (A | MSGLIST), 0, MMNDEL
     DS(N_("Type each message of <message-list> on the terminal")) },
   { "type", &c_type, (A | MSGLIST), 0, MMNDEL
     DS(N_("Type each message of <message-list> on the terminal")) },
   { "Type", &c_Type, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like \"print\", but prints all headers and parts")) },
   { "Print", &c_Type, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like \"print\", but prints all headers and parts")) },
   { "visual", &c_visual, (A | I | MSGLIST), 0, MMNORM
     DS(N_("Edit <message-list>")) },
   { "top", &c_top, (A | MSGLIST), 0, MMNDEL
     DS(N_("Print top few lines of <message-list>")) },
   { "touch", &c_stouch, (A | W | MSGLIST), 0, MMNDEL
     DS(N_("Mark <message-list> for saving in *mbox*")) },
   { "preserve", &c_preserve, (A | W | MSGLIST), 0, MMNDEL
     DS(N_("Save <message-list> in system mailbox instead of *MBOX*")) },
   { "delete", &c_delete, (A | W | P | MSGLIST), 0, MMNDEL
     DS(N_("Delete <message-list>")) },
   { "dp", &c_deltype, (A | W | MSGLIST), 0, MMNDEL
     DS(N_("Delete the current message, then print the next")) },
   { "dt", &c_deltype, (A | W | MSGLIST), 0, MMNDEL
     DS(N_("Delete the current message, then print the next")) },
   { "undelete", &c_undelete, (A | P | MSGLIST), MDELETED,MMNDEL
     DS(N_("Un\"delete\" <message-list>")) },
   { "unset", &c_unset, (H | M | RAWLIST), 1, 1000
     DS(N_("Unset <option-list>")) },
   { "unsetenv", &c_unsetenv, (H | M | RAWLIST), 1, 1000
     DS(N_("Unset <option-list>, also in the program environment")) },
   { "mail", &c_sendmail, (R | M | I | STRLIST), 0, 0
     DS(N_("Compose mail; recipients may be given as arguments")) },
   { "Mail", &c_Sendmail, (R | M | I | STRLIST), 0, 0
     DS(N_("Like \"mail\", but derive filename from first recipient")) },
   { "mbox", &c_mboxit, (A | W | MSGLIST), 0, 0
     DS(N_("Indicate that <message-list> is to be stored in *mbox*")) },
   { "more", &c_more, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like \"type\"/\"print\", put print \\f between messages")) },
   { "page", &c_more, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like \"type\"/\"print\", put print \\f between messages")) },
   { "More", &c_More, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like \"Type\"/\"Print\", put print \\f between messages")) },
   { "Page", &c_More, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like \"Type\"/\"Print\", put print \\f between messages")) },
   { "unread", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(N_("Mark <message-list> as not being read")) },
   { "Unread", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(N_("Mark <message-list> as not being read")) },
   { "new", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(N_("Mark <message-list> as not being read")) },
   { "New", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(N_("Mark <message-list> as not being read")) },
   { "!", &c_shell, (I | STRLIST), 0, 0 /* XXX I? */
     DS(N_("Execute <shell-command>")) },
   { "copy", &c_copy, (A | M | STRLIST), 0, 0
     DS(N_("Copy <message-list>, but don't mark them for deletion")) },
   { "Copy", &c_Copy, (A | M | STRLIST), 0, 0
     DS(N_("Like \"copy\", but derive filename from first sender")) },
   { "chdir", &c_chdir, (M | RAWLIST), 0, 1
     DS(N_("Change CWD to the specified/the login directory")) },
   { "cd", &c_chdir, (M | RAWLIST), 0, 1
     DS(N_("Change CWD to the specified/the login directory")) },
   { "save", &c_save, (A | STRLIST), 0, 0
     DS(N_("Append <message-list> to <file>")) },
   { "Save", &c_Save, (A | STRLIST), 0, 0
     DS(N_("Like \"save\", but derive filename from first sender")) },
   { "source", &c_source, (M | RAWLIST), 1, 1
     DS(N_("Read commands from <file>")) },
   { "set", &c_set, (H | M | RAWLIST), 0, 1000
     DS(N_("Print all variables, or set (a) <variable>(s)")) },
   { "setenv", &c_setenv, (H | M | RAWLIST), 1, 1000
     DS(N_("Set (a) <variable>(s) and export into the program environment")) },
   { "shell", &c_dosh, (I | NOLIST), 0, 0
     DS(N_("Invoke an interactive shell")) },
   { "unalias", &c_unalias, (M | RAWLIST), 1, 1000
     DS(N_("Un\"alias\" <name-list> (\"*\" for all)")) },
   { "write", &c_write, (A | STRLIST), 0, 0
     DS(N_("Write (append) to <file>")) },
   { "from", &c_from, (A | MSGLIST), 0, MMNORM
     DS(N_("Show message headers of <message-list>")) },
   { "file", &c_file, (T | M | RAWLIST), 0, 1
     DS(N_("Open a new <mailbox> or show the current one")) },
   { "followup", &c_followup, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Like \"reply\", but derive filename from first sender")) },
   { "followupall", &c_followupall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Like \"reply\", but derive filename from first sender")) },
   { "followupsender", &c_followupsender, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Like \"Followup\", but always reply to the sender only")) },
   { "folder", &c_file, (T | M | RAWLIST), 0, 1
     DS(N_("Open a new <mailbox> or show the current one")) },
   { "folders", &c_folders, (T | M | RAWLIST), 0, 1
     DS(N_("List mailboxes below the given or the global folder")) },
   { "z", &c_scroll, (A | M | STRLIST), 0, 0
     DS(N_("Scroll header display as indicated by the argument (0,-,+,$)")) },
   { "Z", &c_Scroll, (A | M | STRLIST), 0, 0
     DS(N_("Like \"z\", but continues to the next flagged message")) },
   { "headers", &c_headers, (A | M | MSGLIST), 0, MMNDEL
     DS(N_("Print a page of headers (with the first of <message> if given)")) },
   { "help", &c_help, (H | M | RAWLIST), 0, 1
     DS(N_("Show command help (for the given one)")) },
   { "?", &c_help, (H | M | RAWLIST), 0, 1
     DS(N_("Show command help (for the given one)")) },
   { "=", &c_pdot, (A | NOLIST), 0, 0
     DS(N_("Show current message number")) },
   { "Reply", &c_Reply, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Respond", &c_Reply, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Followup", &c_Followup, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Like \"Reply\", but derive filename from first sender")) },
   { "reply", &c_reply, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator and recipients of <message-list>")) },
   { "replyall", &c_replyall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator and recipients of <message-list>")) },
   { "replysender", &c_replysender, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "respond", &c_reply, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originators and recipients of <message-list>")) },
   { "respondall", &c_replyall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originators and recipients of <message-list>")) },
   { "respondsender", &c_replysender, (A | R | I | MSGLIST),0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Resend", &c_Resend, (A | R | STRLIST), 0, MMNDEL
     DS(N_("Like \"resend\", but don't add Resent-* headers")) },
   { "Redirect", &c_Resend, (A | R | STRLIST), 0, MMNDEL
     DS(N_("Like \"resend\", but don't add Resent-* headers")) },
   { "resend", &c_resend, (A | R | STRLIST), 0, MMNDEL
     DS(N_("Resend <message-list> to <user>, add Resent-* headers")) },
   { "redirect", &c_resend, (A | R | STRLIST), 0, MMNDEL
     DS(N_("Resend <message-list> to <user>, add Resent-* headers")) },
   { "Forward", &c_Forward, (A | R | STRLIST), 0, MMNDEL
     DS(N_("Like \"forward\", but derive filename from <address>")) },
   { "Fwd", &c_Forward, (A | R | STRLIST), 0, MMNDEL
     DS(N_("Like \"forward\", but derive filename from <address>")) },
   { "forward", &c_forward, (A | R | STRLIST), 0, MMNDEL
     DS(N_("Forward <message> to <address>")) },
   { "fwd", &c_forward, (A | R | STRLIST), 0, MMNDEL
     DS(N_("Forward <message> to <address>")) },
   { "edit", &c_editor, (A | I | MSGLIST), 0, MMNORM
     DS(N_("Edit <message-list>")) },
   { "echo", &c_echo, (H | M | ECHOLIST), 0, 1000
     DS(N_("Echo given arguments")) },
   { "quit", &_c_quit, NOLIST, 0, 0
     DS(N_("Terminate session, saving messages as necessary")) },
   { "list", &_c_pcmdlist, (H | M | NOLIST), 0, 0
     DS(N_("List all available commands")) },
   { "xit", &c_rexit, (M | NOLIST), 0, 0
     DS(N_("Immediate return to the shell without saving")) },
   { "exit", &c_rexit, (M | NOLIST), 0, 0
     DS(N_("Immediate return to the shell without saving")) },
   { "pipe", &c_pipe, (A | STRLIST), 0, MMNDEL
     DS(N_("Pipe <message-list> to <command>")) },
   { "|", &c_pipe, (A | STRLIST), 0, MMNDEL
     DS(N_("Pipe <message-list> to <command>")) },
   { "Pipe", &c_Pipe, (A | STRLIST), 0, MMNDEL
     DS(N_("Like \"pipe\", but pipes all headers and parts")) },
   { "size", &c_messize, (A | MSGLIST), 0, MMNDEL
     DS(N_("Show size in characters for <message-list>")) },
   { "hold", &c_preserve, (A | W | MSGLIST), 0, MMNDEL
     DS(N_("Save <message-list> in system mailbox instead of *MBOX*")) },
   { "if", &c_if, (F | M | RAWLIST), 1, 1000
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "else", &c_else, (F | M | RAWLIST), 0, 0
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "elif", &c_elif, (F | M | RAWLIST), 1, 1000
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "endif", &c_endif, (F | M | RAWLIST), 0, 0
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "alternates", &c_alternates, (M | RAWLIST), 0, 1000
     DS(N_("Show or define an alternate list for the invoking user")) },
   { "ignore", &c_igfield, (M | RAWLIST), 0, 1000
     DS(N_("Add <header-fields> to the ignored LIST, or show that list")) },
   { "discard", &c_igfield, (M | RAWLIST), 0, 1000
     DS(N_("Add <header-fields> to the ignored LIST, or show that list")) },
   { "retain", &c_retfield, (M | RAWLIST), 0, 1000
     DS(N_("Add <header-fields> to retained list, or show that list")) },
   { "saveignore", &c_saveigfield, (M | RAWLIST), 0, 1000
     DS(N_("Is to \"save\" what \"ignore\" is to \"type\"/\"print\"")) },
   { "savediscard", &c_saveigfield, (M | RAWLIST), 0, 1000
     DS(N_("Is to \"save\" what \"ignore\" is to \"type\"/\"print\"")) },
   { "saveretain", &c_saveretfield, (M | RAWLIST), 0, 1000
     DS(N_("Is to \"save\" what \"retain\" is to \"type\"/\"print\"")) },
   { "unignore", &c_unignore, (M | RAWLIST), 0, 1000
     DS(N_("Un\"ignore\" <header-fields>")) },
   { "unretain", &c_unretain, (M | RAWLIST), 0, 1000
     DS(N_("Un\"retain\" <header-fields>")) },
   { "unsaveignore", &c_unsaveignore, (M | RAWLIST), 0, 1000
     DS(N_("Un\"saveignore\" <header-fields>")) },
   { "unsaveretain", &c_unsaveretain, (M | RAWLIST), 0, 1000
     DS(N_("Un\"saveretain\" <header-fields>")) },
   { "inc", &c_newmail, (A | T | NOLIST), 0, 0
     DS(N_("Check for new mail in current folder")) },
   { "newmail", &c_newmail, (A | T | NOLIST), 0, 0
     DS(N_("Check for new mail in current folder")) },
   { "shortcut", &c_shortcut, (M | RAWLIST), 0, 1000
     DS(N_("Define <shortcut>s and their <expansion>, or list shortcuts")) },
   { "unshortcut", &c_unshortcut, (M | RAWLIST), 1, 1000
     DS(N_("Delete <shortcut-list> (\"*\" for all)")) },
   { "imap", &c_imap_imap, (A | STRLIST), 0, 1000
     DS(N_("Send command strings directly to the IMAP server")) },
   { "account", &c_account, (M | RAWLIST), 0, 1000
     DS(N_("Create or select <account>, or list all accounts")) },
   { "thread", &c_thread, (A | O | MSGLIST), 0, 0
     DS(N_("Create threaded view of current \"folder\"")) },
   { "unthread", &c_unthread, (A | O | MSGLIST), 0, 0
     DS(N_("Disable sorted or threaded mode")) },
   { "connect", &c_connect, (A | NOLIST), 0, 0
     DS(N_("If disconnected, connect to IMAP mailbox")) },
   { "disconnect", &c_disconnect, (A | NDMLIST), 0, 0
     DS(N_("If connected, disconnect from IMAP mailbox")) },
   { "sort", &c_sort, (A | RAWLIST), 0, 1
     DS(N_("Change sorting: date,from,size,spam,status,subject,thread,to")) },
   { "unsort", &c_unthread, (A | MSGLIST), 0, 0
     DS(N_("Disable sorted or threaded mode")) },
   { "cache", &c_cache, (A | MSGLIST), 0, 0
     DS(N_("Read specified <message list> into the IMAP cache")) },
   { "flag", &c_flag, (A | M | MSGLIST), 0, 0
     DS(N_("(Un)Flag <message-list> (for special attention)")) },
   { "unflag", &c_unflag, (A | M | MSGLIST), 0, 0
     DS(N_("(Un)Flag <message-list> (for special attention)")) },
   { "answered", &c_answered, (A | M | MSGLIST), 0, 0
     DS(N_("Mark the given <message list> as \"answered\"")) },
   { "unanswered", &c_unanswered, (A | M | MSGLIST), 0, 0
     DS(N_("Un\"answered\" <message-list>")) },
   { "draft", &c_draft, (A | M | MSGLIST), 0, 0
     DS(N_("Mark <message-list> as draft")) },
   { "undraft", &c_undraft, (A | M | MSGLIST), 0, 0
     DS(N_("Un\"draft\" <message-list>")) },
   { "define", &c_define, (M | RAWLIST), 0, 2
     DS(N_("Define a <macro> or show the currently defined ones")) },
   { "undefine", &c_undefine, (M | RAWLIST), 0, 1000
     DS(N_("Un\"define\" all given <macros>")) },
   { "unaccount", &c_unaccount, (M | RAWLIST), 0, 1000
     DS(N_("Delete all given <accounts>")) },
   { "call", &c_call, (M | RAWLIST), 0, 1
     DS(N_("Call a macro")) },
   { "~", &c_call, (M | RAWLIST), 0, 1
     DS(N_("Call a macro")) },
   { "move", &c_move, (A | M | STRLIST), 0, 0
     DS(N_("Like \"copy\", but mark messages for deletion")) },
   { "mv", &c_move, (A | M | STRLIST), 0, 0
     DS(N_("Like \"copy\", but mark messages for deletion")) },
   { "Move", &c_Move, (A | M | STRLIST), 0, 0
     DS(N_("Like \"move\", but derive filename from first sender")) },
   { "Mv", &c_Move, (A | M | STRLIST), 0, 0
     DS(N_("Like \"move\", but derive filename from first sender")) },
   { "noop", &c_noop, (A | M | RAWLIST), 0, 0
     DS(N_("NOOP command if IMAP or POP folder; else noop")) },
   { "collapse", &c_collapse, (A | MSGLIST), 0, 0
     DS(N_("Collapse thread views for <message-list>")) },
   { "uncollapse", &c_uncollapse, (A | MSGLIST), 0, 0
     DS(N_("Uncollapse <message-list> if in threaded view")) },
   { "verify", &c_verify, (A | MSGLIST), 0, 0
     DS(N_("Verify <message-list>")) },
   { "decrypt", &c_decrypt, (A | M | STRLIST), 0, 0
     DS(N_("Like \"copy\", but decrypt first, if encrypted")) },
   { "Decrypt", &c_Decrypt, (A | M | STRLIST), 0, 0
     DS(N_("Like \"decrypt\", but derive filename from first sender")) },
   { "certsave", &c_certsave, (A | STRLIST), 0, 0
     DS(N_("Save S/MIME certificates of <message-list> to <file>")) },
   { "rename", &c_rename, (M | RAWLIST), 0, 2
     DS(N_("Rename <existing-folder> to <new-folder>")) },
   { "remove", &c_remove, (M | RAWLIST), 0, 1000
     DS(N_("Remove the named folders")) },
   { "show", &c_show, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like \"print\", but show raw message content")) },
   { "Show", &c_show, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like \"print\", but show raw message content")) },
   { "seen", &c_seen, (A | M | MSGLIST), 0, MMNDEL
     DS(N_("Mark <message-list> as seen")) },
   { "Seen", &c_seen, (A | M | MSGLIST), 0, MMNDEL
     DS(N_("Mark <message-list> as seen")) },
   { "fwdignore", &c_fwdigfield, (M | RAWLIST), 0, 1000
     DS(N_("Which header fields are to be ignored with \"forward\"")) },
   { "fwddiscard", &c_fwdigfield, (M | RAWLIST), 0, 1000
     DS(N_("Which header fields are to be ignored with \"forward\"")) },
   { "fwdretain", &c_fwdretfield, (M | RAWLIST), 0, 1000
     DS(N_("Which header fields have to be retained with \"forward\"")) },
   { "unfwdignore", &c_unfwdignore, (M | RAWLIST), 0, 1000
     DS(N_("Un\"fwdignore\" <header-fields>")) },
   { "unfwdretain", &c_unfwdretain, (M | RAWLIST), 0, 1000
     DS(N_("Un\"fwdretain\" <header-fields>")) },
   { "mimetype", &c_mimetype, (M | RAWLIST), 0, 1000
     DS(N_("(Load and) show all known MIME types or define some")) },
   { "unmimetype", &c_unmimetype, (M | RAWLIST), 1, 1000
     DS(N_("Delete <type>s (\"reset\", \"*\" for all; former reinit.s)")) },
   { "spamrate", &c_spam_rate, (A | M | I | MSGLIST), 0, 0
     DS(N_("Rate <message-list> via the spam detector")) },
   { "spamham", &c_spam_ham, (A | M | I | MSGLIST), 0, 0
     DS(N_("Teach the spam detector that <message-list> is ham")) },
   { "spamspam", &c_spam_spam, (A | M | I | MSGLIST), 0, 0
     DS(N_("Teach the spam detector that <message-list> is spam")) },
   { "spamforget", &c_spam_forget, (A | M | I | MSGLIST), 0, 0
     DS(N_("Force the spam detector to \"unlearn\" <message-list>")) },
   { "spamset", &c_spam_set, (A | M | I | MSGLIST), 0, 0
     DS(N_("Set the spam flag for each message in <message-list>")) },
   { "spamclear", &c_spam_clear, (A | M | I | MSGLIST), 0, 0
     DS(N_("Clear the spam flag for each message in <message-list>")) },
   { "ghost", &_c_ghost, (M | RAWLIST), 0, 1000
     DS(N_("Print or create <ghost> (<command>), or list all ghosts")) },
   { "unghost", &_c_unghost, (M | RAWLIST), 1, 1000
     DS(N_("Delete <ghost-list>")) },
   { "localopts", &c_localopts, (H | M | RAWLIST), 1, 1
     DS(N_("Inside `define' / `account': insulate modifications? <boolean>"))},
   { "cwd", &c_cwd, (M | NOLIST), 0, 0
     DS(N_("Print current working directory (CWD)")) },
   { "pwd", &c_cwd, (M | NOLIST), 0, 0
     DS(N_("Print current working directory (CWD)")) },
   { "varshow", &c_varshow, (H | M | RAWLIST), 1, 1000
     DS(N_("Show some informations about the given <variables>")) },
   { "varedit", &c_varedit, (H | I | M | RAWLIST), 1, 1000
     DS(N_("Edit the value(s) of (an) variable(s), or create them")) },
   { "urlencode", &c_urlencode, (H| M | RAWLIST), 1, 1000
     DS(N_("Encode <string-list> for usage in an URL")) },
   { "urldecode", &c_urldecode, (H | M | RAWLIST), 1, 1000
     DS(N_("Decode the URL-encoded <URL-list> into strings")) },
   { "File", &c_File, (T | M | RAWLIST), 0, 1
     DS(N_("Open a new mailbox readonly or show the current mailbox")) },
   { "Folder", &c_File, (T | M | RAWLIST), 0, 1
     DS(N_("Open a new mailbox readonly or show the current mailbox")) },
   { "mlist", &c_mlist, (M | RAWLIST), 0, 1000
     DS(N_("Show all known mailing lists or define some")) },
   { "unmlist", &c_unmlist, (M | RAWLIST), 1, 1000
     DS(N_("Un\"mlist\" <name-list> (\"*\" for all)")) },
   { "mlsubscribe", &c_mlsubscribe, (M | RAWLIST), 0, 1000
     DS(N_("Show all mailing list subscriptions or define some")) },
   { "unmlsubscribe", &c_unmlsubscribe, (M | RAWLIST), 1, 1000
     DS(N_("Un\"mlsubscribe\" <name-list> (\"*\" for all)"))},
   { "Lreply", &c_Lreply, (A | R | I | MSGLIST), 0, MMNDEL
     DS(N_("Mailing-list reply to the given message")) },
   { "features", &_c_features, (H | M | NOLIST), 0, 0
     DS(N_("Show features that are compiled into the MUA")) },
   { "version", &_c_version, (H | M | NOLIST), 0, 0
     DS(N_("Print the MUA version")) },
#ifdef HAVE_HISTORY
   { "history", &c_history, (H | I | M | V | RAWLIST), 0, 1
     DS(N_("<show> (default), <clear> or select <NO> from editor history")) },
#endif
#ifdef HAVE_NETRC
   { "netrc", &c_netrc, (M | RAWLIST), 0, 1
     DS(N_("Either <show> (default) or <clear> the .netrc cache")) },
#endif
#ifdef HAVE_DEBUG
   { "sstats", &c_sstats, (H | I | M | NOLIST), 0, 0
     DS(N_("Print statistics about the auto-reclaimed string store")) },
   { "smemtrace", &c_smemtrace, (H | I | M | NOLIST), 0, 0
     DS(N_("Trace current memory usage afap")) },
#endif
   { NULL, NULL, 0, 0, 0 DS("") }

#undef DS

#undef MSGLIST
#undef STRLIST
#undef RAWLIST
#undef NOLIST
#undef NDMLIST
#undef ECHOLIST
#undef ARG_ARGMASK
#undef A
#undef F
#undef H
#undef I
#undef M
#undef P
#undef R
#undef T
#undef V
#undef W
#undef O

/* s-it-mode */
