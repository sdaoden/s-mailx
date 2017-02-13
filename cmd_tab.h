/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ This is included by ./lex_input.c and defines the command array.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2017 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
 */
/*
 * Copyright (c) 1980, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
#define MSGLIST ARG_MSGLIST
#define STRLIST ARG_STRLIST
#define RAWLIST ARG_RAWLIST
#define NOLIST ARG_NOLIST
#define NDMLIST ARG_NDMLIST
#define WYSHLIST ARG_WYSHLIST
#  define WYRALIST ARG_WYRALIST

#define ARG_ARGMASK ARG_ARGMASK
#define A ARG_A
#define F ARG_F
#define G ARG_G
#define H ARG_H
#define I ARG_I
#define M ARG_M
#define O ARG_O
#define P ARG_P
#define R ARG_R
#define S ARG_S
#define T ARG_T
#define V ARG_V
#define W ARG_W
#define X ARG_X
#define EM ARG_EM

   /* Note: the first command in here may NOT expand to an unsupported one! */
   { "next", &c_next, (A | NDMLIST), 0, MMNDEL
     DS(N_("Goes to the next message (-list) and prints it")) },
   { "alias", &c_alias, (M | RAWLIST), 0, 1000
     DS(N_("Show all (or <alias>), or (re)define <alias> to <:data:>")) },
   { "print", &c_type, (A | MSGLIST), 0, MMNDEL
     DS(N_("Type all messages of <msglist>, honouring `ignore' / `retain'")) },
   { "type", &c_type, (A | MSGLIST), 0, MMNDEL
     DS(N_("Type all messages of <msglist>, honouring `ignore' / `retain'")) },
   { "Type", &c_Type, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like `type', but bypass `ignore' / `retain'")) },
   { "Print", &c_Type, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like `print', but bypass `ignore' / `retain'")) },
   { "visual", &c_visual, (A | I | S | MSGLIST), 0, MMNORM
     DS(N_("Edit <msglist>")) },
   { "top", &c_top, (A | MSGLIST), 0, MMNDEL
     DS(N_("Type first *toplines* of all messages in <msglist>")) },
   { "Top", &c_Top, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like `top', but bypass `ignore' / `retain'")) },
   { "touch", &c_stouch, (A | W | MSGLIST), 0, MMNDEL
     DS(N_("Mark <msglist> for saving in *mbox*")) },
   { "preserve", &c_preserve, (A | S | W | MSGLIST), 0, MMNDEL
     DS(N_("Save <msglist> in system mailbox instead of *MBOX*")) },
   { "delete", &c_delete, (A | W | P | MSGLIST), 0, MMNDEL
     DS(N_("Delete <msglist>")) },
   { "dp", &c_deltype, (A | W | MSGLIST), 0, MMNDEL
     DS(N_("Delete the current message, then type the next")) },
   { "dt", &c_deltype, (A | W | MSGLIST), 0, MMNDEL
     DS(N_("Delete the current message, then type the next")) },
   { "undelete", &c_undelete, (A | P | MSGLIST), MDELETED,MMNDEL
     DS(N_("Un`delete' <msglist>")) },
   { "unset", &c_unset, (G | M | X | WYRALIST), 1, 1000
     DS(N_("Unset <option-list>")) },
   { "mail", &c_sendmail, (I | M | R | S | STRLIST), 0, 0
     DS(N_("Compose mail; recipients may be given as arguments")) },
   { "Mail", &c_Sendmail, (I | M | R | S | STRLIST), 0, 0
     DS(N_("Like `mail', but derive filename from first recipient")) },
   { "mbox", &c_mboxit, (A | W | MSGLIST), 0, 0
     DS(N_("Indicate that <msglist> is to be stored in *MBOX*")) },
   { "more", &c_more, (A | MSGLIST), 0, MMNDEL
     DS(N_("Invoke the pager on the given messages")) },
   { "page", &c_more, (A | MSGLIST), 0, MMNDEL
     DS(N_("Invoke the pager on the given messages")) },
   { "More", &c_More, (A | MSGLIST), 0, MMNDEL
     DS(N_("Invoke the pager on the given messages")) },
   { "Page", &c_More, (A | MSGLIST), 0, MMNDEL
     DS(N_("Invoke the pager on the given messages")) },
   { "unread", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(N_("Mark <msglist> as not being read")) },
   { "Unread", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(N_("Mark <msglist> as not being read")) },
   { "new", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(N_("Mark <msglist> as not being read")) },
   { "New", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(N_("Mark <msglist> as not being read")) },
   { "!", &c_shell, (S | STRLIST), 0, 0
     DS(N_("Execute <shell-command>")) },
   { "copy", &c_copy, (A | M | STRLIST), 0, 0
     DS(N_("Copy <msglist>, but don't mark them for deletion")) },
   { "Copy", &c_Copy, (A | M | S | STRLIST), 0, 0
     DS(N_("Like `copy', but derive filename from first sender")) },
   { "chdir", &c_chdir, (M | WYRALIST), 0, 1
     DS(N_("Change CWD to the specified/the login directory")) },
   { "cd", &c_chdir, (M | X | WYRALIST), 0, 1
     DS(N_("Change working directory to the specified/the login directory")) },
   { "save", &c_save, (A | STRLIST), 0, 0
     DS(N_("Append <msglist> to <file>")) },
   { "Save", &c_Save, (A | S | STRLIST), 0, 0
     DS(N_("Like `save', but derive filename from first sender")) },
   { "source", &c_source, (M | WYRALIST), 1, 1
     DS(N_("Read commands from <file>")) },
   { "source_if", &c_source_if, (M | WYRALIST), 1, 1
     DS(N_("If <file> can be opened successfully, read commands from it")) },
   { "set", &c_set, (G | M | X | WYRALIST), 0, 1000
     DS(N_("Print all variables, or set (a) <variable>(s)")) },
   { "shell", &c_dosh, (I | S | NOLIST), 0, 0
     DS(N_("Invoke an interactive shell")) },
   { "unalias", &c_unalias, (M | RAWLIST), 1, 1000
     DS(N_("Un`alias' <name-list> (* for all)")) },
   { "write", &c_write, (A | STRLIST), 0, 0
     DS(N_("Write (append) to <file>")) },
   { "from", &c_from, (A | MSGLIST), 0, MMNORM
     DS(N_("Type (matching) headers of <msglist> (a search specification)")) },
   { "search", &c_from, (A | MSGLIST), 0, MMNORM
     DS(N_("Search for <msglist>, type matching headers")) },
   { "file", &c_file, (M | T | WYRALIST), 0, 1
     DS(N_("Open a new <mailbox> or show the current one")) },
   { "followup", &c_followup, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Like `reply', but derive filename from first sender")) },
   { "followupall", &c_followupall, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Like `reply', but derive filename from first sender")) },
   { "followupsender", &c_followupsender, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Like `Followup', but always reply to the sender only")) },
   { "folder", &c_file, (M | T | WYRALIST), 0, 1
     DS(N_("Open a new <mailbox> or show the current one")) },
   { "folders", &c_folders, (M | T | WYRALIST), 0, 1
     DS(N_("List mailboxes below the given or the global folder")) },
   { "z", &c_scroll, (A | M | STRLIST), 0, 0
     DS(N_("Scroll header display as indicated by the argument (0,-,+,$)")) },
   { "Z", &c_Scroll, (A | M | STRLIST), 0, 0
     DS(N_("Like `z', but continues to the next flagged message")) },
   { "headers", &c_headers, (A | M | MSGLIST), 0, MMNDEL
     DS(N_("Type a page of headers (with the first of <msglist> if given)")) },
   { "help", &a_lex_c_help, (H | M | X | WYRALIST), 0, 1
     DS(N_("Show help [[Option] for the given command]]")) },
   { "?", &a_lex_c_help, (H | M | X | WYRALIST), 0, 1
     DS(N_("Show help [[Option] for the given command]]")) },
   { "=", &c_pdot, (A | NOLIST), 0, 0
     DS(N_("Show current message number")) },
   { "Reply", &c_Reply, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Respond", &c_Reply, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Followup", &c_Followup, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Like `Reply', but derive filename from first sender")) },
   { "reply", &c_reply, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator and recipients of <msglist>")) },
   { "replyall", &c_replyall, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator and recipients of <msglist>")) },
   { "replysender", &c_replysender, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "respond", &c_reply, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originators and recipients of <msglist>")) },
   { "respondall", &c_replyall, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Reply to originators and recipients of <msglist>")) },
   { "respondsender", &c_replysender, (A | I | R | S | MSGLIST),0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Resend", &c_Resend, (A | R | S | STRLIST), 0, MMNDEL
     DS(N_("Like `resend', but don't add Resent-* header lines")) },
   { "Redirect", &c_Resend, (A | R | S | STRLIST), 0, MMNDEL
     DS(N_("Like `resend', but don't add Resent-* header lines")) },
   { "resend", &c_resend, (A | R | S | STRLIST), 0, MMNDEL
     DS(N_("Resend <msglist> to <user>, add Resent-* header lines")) },
   { "redirect", &c_resend, (A | R | S | STRLIST), 0, MMNDEL
     DS(N_("Resend <msglist> to <user>, add Resent-* header lines")) },
   { "Forward", &c_Forward, (A | R | S | STRLIST), 0, MMNDEL
     DS(N_("Like `forward', but derive filename from <address>")) },
   { "Fwd", &c_Forward, (A | R | S | STRLIST), 0, MMNDEL
     DS(N_("Like `forward', but derive filename from <address>")) },
   { "forward", &c_forward, (A | R | S | STRLIST), 0, MMNDEL
     DS(N_("Forward <message> to <address>")) },
   { "fwd", &c_forward, (A | R | S | STRLIST), 0, MMNDEL
     DS(N_("Forward <message> to <address>")) },
   { "edit", &c_editor, (G | A | I | S | MSGLIST), 0, MMNORM
     DS(N_("Edit <msglist>")) },
   { "echo", &c_echo, (G | M | X | WYSHLIST), 0, 1000
     DS(N_("Echo arguments, and a trailing newline, to standard output")) },
   { "echoerr", &c_echoerr, (G | M | X | WYSHLIST), 0, 1000
     DS(N_("Echo arguments, and a trailing newline, to standard error")) },
   { "echon", &c_echon, (G | M | X | WYSHLIST), 0, 1000
     DS(N_("Echo arguments, without a trailing newline, to standard output")) },
   { "echoerrn", &c_echoerrn, (G | M | X | WYSHLIST), 0, 1000
     DS(N_("Echo arguments, without a trailing newline, to standard error")) },
   { "quit", &a_lex_c_quit, NOLIST, 0, 0
     DS(N_("Terminate session, saving messages as necessary")) },
   { "list", &a_lex_c_list, (H | M | STRLIST), 0, 0
     DS(N_("List all commands (with argument: in prefix search order)")) },
   { "xit", &a_lex_c_exit, (M | X | NOLIST), 0, 0
     DS(N_("Immediately return to the shell without saving")) },
   { "exit", &a_lex_c_exit, (M | X | NOLIST), 0, 0
     DS(N_("Immediately return to the shell without saving")) },
   { "pipe", &c_pipe, (A | STRLIST), 0, MMNDEL
     DS(N_("Pipe <msglist> to <command>, honouring `ignore' / `retain'")) },
   { "|", &c_pipe, (A | STRLIST), 0, MMNDEL
     DS(N_("Pipe <msglist> to <command>, honouring `ignore' / `retain'")) },
   { "Pipe", &c_Pipe, (A | STRLIST), 0, MMNDEL
     DS(N_("Like `pipe', but bypass `ignore' / `retain'")) },
   { "size", &c_messize, (A | MSGLIST), 0, MMNDEL
     DS(N_("Show size in bytes for <msglist>")) },
   { "hold", &c_preserve, (A | S | W | MSGLIST), 0, MMNDEL
     DS(N_("Save <msglist> in system mailbox instead of *MBOX*")) },
   { "if", &c_if, (G | F | M | X | RAWLIST), 1, 1000
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "else", &c_else, (G| F | M | X | RAWLIST), 0, 0
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "elif", &c_elif, (G| F | M | X | RAWLIST), 1, 1000
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "endif", &c_endif, (G| F | M | X | RAWLIST), 0, 0
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "alternates", &c_alternates, (M | RAWLIST), 0, 1000
     DS(N_("Show or define an alternate address list for the invoking user")) },
   { "ignore", &c_ignore, (M | RAWLIST), 0, 1000
     DS(N_("Add <header-list> to the ignored LIST, or show that list")) },
   { "discard", &c_ignore, (M | RAWLIST), 0, 1000
     DS(N_("Add <header-list> to the ignored LIST, or show that list")) },
   { "retain", &c_retain, (M | RAWLIST), 0, 1000
     DS(N_("Add <header-list> to retained list, or show that list")) },
   { "headerpick", &c_headerpick, (M | WYSHLIST), 0, 1000
     DS(N_("Header selection: [<context> [<type> [<action> <header-list>]]]"))},
   { "saveignore", &c_saveignore, (O | M | RAWLIST), 0, 1000
     DS(N_("Is to `save' what `ignore' is to `type' / `print'")) },
   { "savediscard", &c_saveignore, (O | M | RAWLIST), 0, 1000
     DS(N_("Is to `save' what `ignore' is to `type' / `print'")) },
   { "saveretain", &c_saveretain, (O | M | RAWLIST), 0, 1000
     DS(N_("Is to `save' what `retain' is to `type' / `print'")) },
   { "unignore", &c_unignore, (M | RAWLIST), 0, 1000
     DS(N_("Un`ignore' <header-list>")) },
   { "unretain", &c_unretain, (M | RAWLIST), 0, 1000
     DS(N_("Un`retain' <header-list>")) },
   { "unsaveignore", &c_unsaveignore, (O | M | RAWLIST), 0, 1000
     DS(N_("Un`saveignore' <header-list>")) },
   { "unsaveretain", &c_unsaveretain, (O | M | RAWLIST), 0, 1000
     DS(N_("Un`saveretain' <header-list>")) },
   { "newmail", &c_newmail, (A | T | NOLIST), 0, 0
     DS(N_("Check for new mail in current folder")) },
   { "shortcut", &c_shortcut, (M | WYRALIST), 0, 1000
     DS(N_("Define <shortcut>s and their <expansion>, or list shortcuts")) },
   { "unshortcut", &c_unshortcut, (M | WYRALIST), 1, 1000
     DS(N_("Delete <shortcut-list> (* for all)")) },
   { "account", &c_account, (M | RAWLIST), 0, 1000
     DS(N_("Create or select <account>, or list all accounts")) },
   { "thread", &c_thread, (A | O | MSGLIST), 0, 0
     DS(N_("Create threaded view of the current `file'")) },
   { "unthread", &c_unthread, (A | O | MSGLIST), 0, 0
     DS(N_("Disable sorted or threaded mode")) },
   { "sort", &c_sort, (A | RAWLIST), 0, 1
     DS(N_("Change sorting to: date,from,size,spam,status,subject,thread,to"))},
   { "unsort", &c_unthread, (A | MSGLIST), 0, 0
     DS(N_("Disable sorted or threaded mode")) },
   { "flag", &c_flag, (A | M | MSGLIST), 0, 0
     DS(N_("(Un)Flag <msglist> (for special attention)")) },
   { "unflag", &c_unflag, (A | M | MSGLIST), 0, 0
     DS(N_("(Un)Flag <msglist> (for special attention)")) },
   { "answered", &c_answered, (A | M | MSGLIST), 0, 0
     DS(N_("Mark the given <msglist> as answered")) },
   { "unanswered", &c_unanswered, (A | M | MSGLIST), 0, 0
     DS(N_("Un`answered' <msglist>")) },
   { "draft", &c_draft, (A | M | MSGLIST), 0, 0
     DS(N_("Mark <msglist> as draft")) },
   { "undraft", &c_undraft, (A | M | MSGLIST), 0, 0
     DS(N_("Un`draft' <msglist>")) },
   { "define", &c_define, (M | X | RAWLIST), 0, 2
     DS(N_("Define a <macro> or show the currently defined ones")) },
   { "undefine", &c_undefine, (M | X | RAWLIST), 1, 1000
     DS(N_("Un`define' all given <macros> (* for all)")) },
   { "unaccount", &c_unaccount, (M | RAWLIST), 1, 1000
     DS(N_("Delete all given <accounts> (* for all)")) },
   { "call", &c_call, (M | X | WYSHLIST), 1, 1000
     DS(N_("Call macro <name>")) },
   { "~", &c_call, (M | X | WYSHLIST), 1, 1000
     DS(N_("Call a macro")) },
   { "call_if", &c_call_if, (M | X | WYRALIST), 1, 100
     DS(N_("Call macro <name> if it exists")) },
   { "shift", &c_shift, (M | X | WYSHLIST), 0, 1
     DS(N_("In a `call'ed macro, shift positional parameters")) },
   { "return", &c_return, (M | X | EM | WYSHLIST), 0, 2
     DS(N_("Return control [with <return value> [<exit status>]] from macro"))},
   { "move", &c_move, (A | M | STRLIST), 0, 0
     DS(N_("Like `copy', but mark messages for deletion")) },
   { "mv", &c_move, (A | M | STRLIST), 0, 0
     DS(N_("Like `copy', but mark messages for deletion")) },
   { "Move", &c_Move, (A | M | S | STRLIST), 0, 0
     DS(N_("Like `move', but derive filename from first sender")) },
   { "Mv", &c_Move, (A | M | S | STRLIST), 0, 0
     DS(N_("Like `move', but derive filename from first sender")) },
   { "noop", &c_noop, (A | M | RAWLIST), 0, 0
     DS(N_("NOOP command if current `file' is accessed via network")) },
   { "collapse", &c_collapse, (A | MSGLIST), 0, 0
     DS(N_("Collapse thread views for <msglist>")) },
   { "uncollapse", &c_uncollapse, (A | MSGLIST), 0, 0
     DS(N_("Uncollapse <msglist> if in threaded view")) },
   { "verify", &c_verify, (A | MSGLIST), 0, 0
     DS(N_("Verify <msglist>")) },
   { "decrypt", &c_decrypt, (A | M | STRLIST), 0, 0
     DS(N_("Like `copy', but decrypt first, if encrypted")) },
   { "Decrypt", &c_Decrypt, (A | M | S | STRLIST), 0, 0
     DS(N_("Like `decrypt', but derive filename from first sender")) },
   { "certsave", &c_certsave, (A | STRLIST), 0, 0
     DS(N_("Save S/MIME certificates of <msglist> to <file>")) },
   { "rename", &c_rename, (M | RAWLIST), 0, 2
     DS(N_("Rename <existing-folder> to <new-folder>")) },
   { "remove", &c_remove, (M | WYRALIST), 0, 1000
     DS(N_("Remove the named folders")) },
   { "show", &c_show, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like `type', but show raw message content of <msglist>")) },
   { "Show", &c_show, (A | MSGLIST), 0, MMNDEL
     DS(N_("Like `Type', but show raw message content of <msglist>")) },
   { "seen", &c_seen, (A | M | MSGLIST), 0, MMNDEL
     DS(N_("Mark <msglist> as seen")) },
   { "Seen", &c_seen, (A | M | MSGLIST), 0, MMNDEL
     DS(N_("Mark <msglist> as seen")) },
   { "fwdignore", &c_fwdignore, (O | M | RAWLIST), 0, 1000
     DS(N_("Control <header-list> to be ignored with `forward'")) },
   { "fwddiscard", &c_fwdignore, (O | M | RAWLIST), 0, 1000
     DS(N_("Control <header-list> to be ignored with `forward'")) },
   { "fwdretain", &c_fwdretain, (O | M | RAWLIST), 0, 1000
     DS(N_("Control <header-list> to be retained with `forward'")) },
   { "unfwdignore", &c_unfwdignore, (O | M | RAWLIST), 0, 1000
     DS(N_("Un`fwdignore' <header-list>")) },
   { "unfwdretain", &c_unfwdretain, (O | M | RAWLIST), 0, 1000
     DS(N_("Un`fwdretain' <header-list>")) },
   { "mimetype", &c_mimetype, (M | WYRALIST), 0, 1000
     DS(N_("(Load and) show all known MIME types, or define some")) },
   { "unmimetype", &c_unmimetype, (M | WYRALIST), 1, 1000
     DS(N_("Delete <type>s (reset, * for all; former reinitializes)")) },
   { "spamrate", &c_spam_rate, (A | M | MSGLIST), 0, 0
     DS(N_("Rate <msglist> via the spam detector")) },
   { "spamham", &c_spam_ham, (A | M | MSGLIST), 0, 0
     DS(N_("Teach the spam detector that <msglist> is ham")) },
   { "spamspam", &c_spam_spam, (A | M | MSGLIST), 0, 0
     DS(N_("Teach the spam detector that <msglist> is spam")) },
   { "spamforget", &c_spam_forget, (A | M | MSGLIST), 0, 0
     DS(N_("Force the spam detector to unlearn <msglist>")) },
   { "spamset", &c_spam_set, (A | M | MSGLIST), 0, 0
     DS(N_("Set the spam flag for each message in <msglist>")) },
   { "spamclear", &c_spam_clear, (A | M | MSGLIST), 0, 0
     DS(N_("Clear the spam flag for each message in <msglist>")) },
   { "localopts", &c_localopts, (H | M | X | RAWLIST), 1, 1
     DS(N_("Inside `define' / `account': isolate modifications? <boolean>"))},
   { "cwd", &c_cwd, (M | X | NOLIST), 0, 0
     DS(N_("Print current working directory (CWD)")) },
   { "pwd", &c_cwd, (M | X | NOLIST), 0, 0
     DS(N_("Print current working directory (CWD)")) },
   { "varshow", &c_varshow, (G | M | X | WYRALIST), 1, 1000
     DS(N_("Show some informations about the given <variables>")) },
   { "varedit", &c_varedit, (G | I | M | WYRALIST), 1, 1000
     DS(N_("Edit the value(s) of (an) variable(s), or create them")) },
   { "vexpr", &c_vexpr, (G | M | V | X | EM | WYSHLIST), 2, 1000
     DS(N_("Evaluate according to <operator> any <:arguments:>")) },
   { "File", &c_File, (M | T | WYRALIST), 0, 1
     DS(N_("Open a new mailbox readonly, or show the current mailbox")) },
   { "Folder", &c_File, (M | T | WYRALIST), 0, 1
     DS(N_("Open a new mailbox readonly, or show the current mailbox")) },
   { "mlist", &c_mlist, (M | WYRALIST), 0, 1000
     DS(N_("Show all known mailing lists or define some")) },
   { "unmlist", &c_unmlist, (M | WYRALIST), 1, 1000
     DS(N_("Un`mlist' <name-list> (* for all)")) },
   { "mlsubscribe", &c_mlsubscribe, (M | WYRALIST), 0, 1000
     DS(N_("Show all mailing list subscriptions or define some")) },
   { "unmlsubscribe", &c_unmlsubscribe, (M | WYRALIST), 1, 1000
     DS(N_("Un`mlsubscribe' <name-list> (* for all)"))},
   { "Lreply", &c_Lreply, (A | I | R | S | MSGLIST), 0, MMNDEL
     DS(N_("Mailing-list reply to the given <msglist>")) },
   { "errors", &c_errors, (H | I | M | RAWLIST), 0, 1
     DS(N_("Either [<show>] or <clear> the error message ring")) },
   { "dotmove", &c_dotmove, (A | STRLIST), 1, 1
     DS(N_("Move the dot up <-> or down <+> by one")) },

   { "eval", &a_lex_c_eval, (G | M | X | EM | WYSHLIST), 1, 1000
     DS(N_("Construct command from <:arguments:>, reuse its $?")) },
   { "ghost", &a_lex_c_ghost, (M | X | WYRALIST), 0, 1000
     DS(N_("Print or create <ghost> [<command>], or list all ghosts")) },
   { "unghost", &a_lex_c_unghost, (M | X | WYRALIST), 1, 1000
     DS(N_("Delete <ghost-list>")) },
   { "read", &a_lex_c_read, (G | M | X | EM | WYSHLIST), 1, 1000
     DS(N_("Read a line from standard input into <variable>(s)")) },
   { "version", &a_lex_c_version, (H | M | X | NOLIST), 0, 0
     DS(N_("Show the version and feature set of the program")) },

   { "history", &c_history, (H | I | M | RAWLIST), 0, 1
     DS(N_("<show> (default), <clear> or select <NO> from editor history")) },
   { "bind", &c_bind, (M | STRLIST), 1, 1000
     DS(N_("For <context> (base), [<show>] or bind <key[:,key:]> [<:data:>]"))},
   { "unbind", &c_unbind, (M | STRLIST), 2, 2
     DS(N_("Un`bind' <context> <key[:,key:]> (* for all)")) },

   { "netrc", &c_netrc, (M | RAWLIST), 0, 1
     DS(N_("[<show>], <load> or <clear> the .netrc cache")) },

   { "colour", &c_colour, (M | WYSHLIST), 1, 4
     DS(N_("Show colour settings of <type> (1, 8, 256, all) or define one")) },
   { "uncolour", &c_uncolour, (M | WYSHLIST), 2, 3
     DS(N_("Un`colour' <type> <mapping> (* for all) [<precondition>]")) },

   { "environ", &c_environ, (G | M | X | WYSHLIST), 2, 1000
     DS(N_("<link|set|unset> (an) environment <variable>(s)")) },

   { "addrcodec", &c_addrcodec, (G | M | V | X | EM | WYSHLIST), 1, 1000
     DS(N_("Form an address of <:arguments:>")) },
   { "urlcodec", &c_urlcodec, (G | M | V | X | EM | WYSHLIST), 2, 1000
     DS(N_("URL percent <[path]enc[ode]|[path]dec[ode]> <:arguments:>")) },
      { "urlencode", &c_urlencode, (O | G | M | X | WYRALIST), 1, 1000
        DS(N_("Encode <string-list> for usage in an URL")) },
      { "urldecode", &c_urldecode, (O | G | M | X | WYRALIST), 1, 1000
        DS(N_("Decode the URL-encoded <URL-list> into strings")) },

#ifdef HAVE_MEMORY_DEBUG
   { "memtrace", &c_memtrace, (H | I | M | NOLIST), 0, 0
     DS(N_("Trace current memory usage afap")) },
#endif
#ifdef HAVE_DEVEL
   { "sigstate", &c_sigstate, (H | I | M | STRLIST), 0, 0
     DS(N_("Show signal handler states")) },
#endif

#  undef WYRALIST
#undef WYSHLIST
#undef MSGLIST
#undef STRLIST
#undef RAWLIST
#undef NOLIST
#undef NDMLIST

#undef ARG_ARGMASK
#undef A
#undef F
#undef G
#undef H
#undef I
#undef M
#undef O
#undef P
#undef R
#undef T
#undef V
#undef W
#undef X
#undef EM

/* s-it-mode */
