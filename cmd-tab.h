/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ This is included by ./go.c and defines the command array.
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

#undef MAC
#define MAC (n_MAXARGC - 1)

/* Some shorter aliases to be able to define a command in two lines */
#define TMSGLST n_CMD_ARG_TYPE_MSGLIST
#define TNDMLST n_CMD_ARG_TYPE_NDMLIST
#define TRAWDAT n_CMD_ARG_TYPE_RAWDAT
#  define TSTRING n_CMD_ARG_TYPE_STRING
#define TWYSH n_CMD_ARG_TYPE_WYSH
#  define TRAWLST n_CMD_ARG_TYPE_RAWLIST
#  define TWYRA n_CMD_ARG_TYPE_WYRA

#define A n_CMD_ARG_A
#define F n_CMD_ARG_F
#define G n_CMD_ARG_G
#define H n_CMD_ARG_H
#define I n_CMD_ARG_I
#define M n_CMD_ARG_M
#define O n_CMD_ARG_O
#define P n_CMD_ARG_P
#define R n_CMD_ARG_R
#define S n_CMD_ARG_S
#define T n_CMD_ARG_T
#define V n_CMD_ARG_V
#define W n_CMD_ARG_W
#define X n_CMD_ARG_X
#define EM n_CMD_ARG_EM

   /* Note: the first command in here may NOT expand to an unsupported one! */
   { "next", &c_next, (A | TNDMLST), 0, MMNDEL
     DS(N_("Goes to the next message (-list) and prints it")) },
   { "alias", &c_alias, (M | TWYRA), 0, MAC
     DS(N_("Show all (or <alias>), or (re)define <alias> to <:data:>")) },
   { "print", &c_type, (A | TMSGLST), 0, MMNDEL
     DS(N_("Type all messages of <msglist>, honouring `ignore' / `retain'")) },
   { "type", &c_type, (A | TMSGLST), 0, MMNDEL
     DS(N_("Type all messages of <msglist>, honouring `ignore' / `retain'")) },
   { "Type", &c_Type, (A | TMSGLST), 0, MMNDEL
     DS(N_("Like `type', but bypass `ignore' / `retain'")) },
   { "Print", &c_Type, (A | TMSGLST), 0, MMNDEL
     DS(N_("Like `print', but bypass `ignore' / `retain'")) },
   { "visual", &c_visual, (A | I | S | TMSGLST), 0, MMNORM
     DS(N_("Edit <msglist>")) },
   { "top", &c_top, (A | TMSGLST), 0, MMNDEL
     DS(N_("Type first *toplines* of all messages in <msglist>")) },
   { "Top", &c_Top, (A | TMSGLST), 0, MMNDEL
     DS(N_("Like `top', but bypass `ignore' / `retain'")) },
   { "touch", &c_stouch, (A | W | TMSGLST), 0, MMNDEL
     DS(N_("Mark <msglist> for saving in *mbox*")) },
   { "preserve", &c_preserve, (A | S | W | TMSGLST), 0, MMNDEL
     DS(N_("Save <msglist> in system mailbox instead of *MBOX*")) },
   { "delete", &c_delete, (A | W | P | TMSGLST), 0, MMNDEL
     DS(N_("Delete <msglist>")) },
   { "dp", &c_deltype, (A | W | TMSGLST), 0, MMNDEL
     DS(N_("Delete the current message, then type the next")) },
   { "dt", &c_deltype, (A | W | TMSGLST), 0, MMNDEL
     DS(N_("Delete the current message, then type the next")) },
   { "undelete", &c_undelete, (A | P | TMSGLST), MDELETED,MMNDEL
     DS(N_("Un`delete' <msglist>")) },
   { "unset", &c_unset, (G | M | X | TWYSH), 1, MAC
     DS(N_("Unset <option-list>")) },
   { "mail", &c_sendmail, (I | M | R | S | TSTRING), 0, 0
     DS(N_("Compose mail; recipients may be given as arguments")) },
   { "Mail", &c_Sendmail, (I | M | R | S | TSTRING), 0, 0
     DS(N_("Like `mail', but derive filename from first recipient")) },
   { "mbox", &c_mboxit, (A | W | TMSGLST), 0, 0
     DS(N_("Indicate that <msglist> is to be stored in *MBOX*")) },
   { "more", &c_more, (A | TMSGLST), 0, MMNDEL
     DS(N_("Invoke the pager on the given messages")) },
   { "page", &c_more, (A | TMSGLST), 0, MMNDEL
     DS(N_("Invoke the pager on the given messages")) },
   { "More", &c_More, (A | TMSGLST), 0, MMNDEL
     DS(N_("Invoke the pager on the given messages")) },
   { "Page", &c_More, (A | TMSGLST), 0, MMNDEL
     DS(N_("Invoke the pager on the given messages")) },
   { "unread", &c_unread, (A | TMSGLST), 0, MMNDEL
     DS(N_("Mark <msglist> as not being read")) },
   { "Unread", &c_unread, (A | TMSGLST), 0, MMNDEL
     DS(N_("Mark <msglist> as not being read")) },
   { "new", &c_unread, (A | TMSGLST), 0, MMNDEL
     DS(N_("Mark <msglist> as not being read")) },
   { "New", &c_unread, (A | TMSGLST), 0, MMNDEL
     DS(N_("Mark <msglist> as not being read")) },
   { "!", &c_shell, (S | TSTRING), 0, 0
     DS(N_("Execute <shell-command>")) },
   { "copy", &c_copy, (A | M | TSTRING), 0, 0
     DS(N_("Copy <msglist>, but don't mark them for deletion")) },
   { "Copy", &c_Copy, (A | M | S | TSTRING), 0, 0
     DS(N_("Like `copy', but derive filename from first sender")) },
   { "chdir", &c_chdir, (M | TWYRA), 0, 1
     DS(N_("Change CWD to the specified/the login directory")) },
   { "cd", &c_chdir, (M | X | TWYRA), 0, 1
     DS(N_("Change working directory to the specified/the login directory")) },
   { "save", &c_save, (A | TSTRING), 0, 0
     DS(N_("Append <msglist> to <file>")) },
   { "Save", &c_Save, (A | S | TSTRING), 0, 0
     DS(N_("Like `save', but derive filename from first sender")) },
   { "source", &c_source, (M | TWYSH), 1, 1
     DS(N_("Read commands from <file>")) },
   { "source_if", &c_source_if, (M | TWYSH), 1, 1
     DS(N_("If <file> can be opened successfully, read commands from it")) },
   { "set", &c_set, (G | M | X | TWYRA), 0, MAC
     DS(N_("Print all variables, or set (a) <variable>(s)")) },
   { "shell", &c_dosh, (I | S | TWYSH), 0, 0
     DS(N_("Invoke an interactive shell")) },
   { "unalias", &c_unalias, (M | TWYRA), 1, MAC
     DS(N_("Un`alias' <name-list> (* for all)")) },
   { "write", &c_write, (A | TSTRING), 0, 0
     DS(N_("Write (append) to <file>")) },
   { "from", &c_from, (A | TMSGLST), 0, MMNORM
     DS(N_("Type (matching) headers of <msglist> (a search specification)")) },
   { "search", &c_from, (A | TMSGLST), 0, MMNORM
     DS(N_("Search for <msglist>, type matching headers")) },
   { "file", &c_file, (M | T | TWYRA), 0, 1
     DS(N_("Open a new <mailbox> or show the current one")) },
   { "followup", &c_followup, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Like `reply', but derive filename from first sender")) },
   { "followupall", &c_followupall, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Like `reply', but derive filename from first sender")) },
   { "followupsender", &c_followupsender, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Like `Followup', but always reply to the sender only")) },
   { "folder", &c_file, (M | T | TWYRA), 0, 1
     DS(N_("Open a new <mailbox> or show the current one")) },
   { "folders", &c_folders, (M | T | TWYRA), 0, 1
     DS(N_("List mailboxes below the given or the global folder")) },
   { "z", &c_scroll, (A | M | TWYSH), 0, 1
     DS(N_("Scroll header display as indicated by the argument (0,-,+,$)")) },
   { "Z", &c_Scroll, (A | M | TWYSH), 0, 1
     DS(N_("Like `z', but continues to the next flagged message")) },
   { "headers", &c_headers, (A | M | TMSGLST), 0, MMNDEL
     DS(N_("Type a page of headers (with the first of <msglist> if given)")) },
   { "help", &a_go_c_help, (M | X | TWYRA), 0, 1
     DS(N_("Show help [[Option] for the given command]]")) },
   { "?", &a_go_c_help, (M | X | TWYRA), 0, 1
     DS(N_("Show help [[Option] for the given command]]")) },
   { "=", &c_pdot, (A | TWYSH), 0, 0
     DS(N_("Show current message number")) },
   { "Reply", &c_Reply, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Respond", &c_Reply, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Followup", &c_Followup, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Like `Reply', but derive filename from first sender")) },
   { "reply", &c_reply, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Reply to originator and recipients of <msglist>")) },
   { "replyall", &c_replyall, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Reply to originator and recipients of <msglist>")) },
   { "replysender", &c_replysender, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "respond", &c_reply, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Reply to originators and recipients of <msglist>")) },
   { "respondall", &c_replyall, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Reply to originators and recipients of <msglist>")) },
   { "respondsender", &c_replysender, (A | I | R | S | TMSGLST),0, MMNDEL
     DS(N_("Reply to originator, exclusively")) },
   { "Resend", &c_Resend, (A | R | S | TSTRING), 0, MMNDEL
     DS(N_("Like `resend', but don't add Resent-* header lines")) },
   { "Redirect", &c_Resend, (A | R | S | TSTRING), 0, MMNDEL
     DS(N_("Like `resend', but don't add Resent-* header lines")) },
   { "resend", &c_resend, (A | R | S | TSTRING), 0, MMNDEL
     DS(N_("Resend <msglist> to <user>, add Resent-* header lines")) },
   { "redirect", &c_resend, (A | R | S | TSTRING), 0, MMNDEL
     DS(N_("Resend <msglist> to <user>, add Resent-* header lines")) },
   { "Forward", &c_Forward, (A | R | S | TSTRING), 0, MMNDEL
     DS(N_("Like `forward', but derive filename from <address>")) },
   { "Fwd", &c_Forward, (A | R | S | TSTRING), 0, MMNDEL
     DS(N_("Like `forward', but derive filename from <address>")) },
   { "forward", &c_forward, (A | R | S | TSTRING), 0, MMNDEL
     DS(N_("Forward <message> to <address>")) },
   { "fwd", &c_forward, (A | R | S | TSTRING), 0, MMNDEL
     DS(N_("Forward <message> to <address>")) },
   { "edit", &c_editor, (G | A | I | S | TMSGLST), 0, MMNORM
     DS(N_("Edit <msglist>")) },
   { "echo", &c_echo, (G | M | X | TWYSH), 0, MAC
     DS(N_("Echo arguments, and a trailing newline, to standard output")) },
   { "echoerr", &c_echoerr, (G | M | X | TWYSH), 0, MAC
     DS(N_("Echo arguments, and a trailing newline, to standard error")) },
   { "echon", &c_echon, (G | M | X | TWYSH), 0, MAC
     DS(N_("Echo arguments, without a trailing newline, to standard output")) },
   { "echoerrn", &c_echoerrn, (G | M | X | TWYSH), 0, MAC
     DS(N_("Echo arguments, without a trailing newline, to standard error")) },
   { "quit", &a_go_c_quit, TWYSH, 0, 0
     DS(N_("Terminate session, saving messages as necessary")) },
   { "list", &a_go_c_list, (M | TWYSH), 0, 1
     DS(N_("List all commands (with argument: in prefix search order)")) },
   { "xit", &a_go_c_exit, (M | X | TWYSH), 0, 0
     DS(N_("Immediately return to the shell without saving")) },
   { "exit", &a_go_c_exit, (M | X | TWYSH), 0, 0
     DS(N_("Immediately return to the shell without saving")) },
   { "pipe", &c_pipe, (A | TSTRING), 0, MMNDEL
     DS(N_("Pipe <msglist> to <command>, honouring `ignore' / `retain'")) },
   { "|", &c_pipe, (A | TSTRING), 0, MMNDEL
     DS(N_("Pipe <msglist> to <command>, honouring `ignore' / `retain'")) },
   { "Pipe", &c_Pipe, (A | TSTRING), 0, MMNDEL
     DS(N_("Like `pipe', but bypass `ignore' / `retain'")) },
   { "size", &c_messize, (A | TMSGLST), 0, MMNDEL
     DS(N_("Show size in bytes for <msglist>")) },
   { "hold", &c_preserve, (A | S | W | TMSGLST), 0, MMNDEL
     DS(N_("Save <msglist> in system mailbox instead of *MBOX*")) },
   { "if", &c_if, (G | F | M | X | TRAWLST), 1, MAC
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "else", &c_else, (G | F | M | X | TWYSH), 0, 0
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "elif", &c_elif, (G | F | M | X | TRAWLST), 1, MAC
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "endif", &c_endif, (G | F | M | X | TWYSH), 0, 0
     DS(N_("Part of the if..elif..else..endif statement")) },
   { "alternates", &c_alternates, (M | TWYRA), 0, MAC
     DS(N_("Show or define an alternate address list for the invoking user")) },
   { "ignore", &c_ignore, (M | TWYRA), 0, MAC
     DS(N_("Add <header-list> to the ignored LIST, or show that list")) },
   { "discard", &c_ignore, (M | TWYRA), 0, MAC
     DS(N_("Add <header-list> to the ignored LIST, or show that list")) },
   { "retain", &c_retain, (M | TWYRA), 0, MAC
     DS(N_("Add <header-list> to retained list, or show that list")) },
   { "saveignore", &c_saveignore, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `headerpick'")) },
   { "savediscard", &c_saveignore, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `headerpick'")) },
   { "saveretain", &c_saveretain, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `headerpick'")) },
   { "unignore", &c_unignore, (M | TWYRA), 0, MAC
     DS(N_("Un`ignore' <header-list>")) },
   { "unretain", &c_unretain, (M | TWYRA), 0, MAC
     DS(N_("Un`retain' <header-list>")) },
   { "unsaveignore", &c_unsaveignore, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `unheaderpick'")) },
   { "unsaveretain", &c_unsaveretain, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `unheaderpick'")) },
   { "newmail", &c_newmail, (A | T | TWYSH), 0, 0
     DS(N_("Check for new mail in current folder")) },
   { "shortcut", &c_shortcut, (M | TWYRA), 0, MAC
     DS(N_("Define <shortcut>s and their <expansion>, or list shortcuts")) },
   { "unshortcut", &c_unshortcut, (M | TWYRA), 1, MAC
     DS(N_("Delete <shortcut-list> (* for all)")) },
   { "account", &c_account, (M | TWYSH), 0, MAC
     DS(N_("Create or select <account>, or list all accounts")) },
   { "thread", &c_thread, (O | A | TMSGLST), 0, 0
     DS(N_("Obsoleted by `sort' \"thread\"")) },
   { "unthread", &c_unthread, (O | A | TMSGLST), 0, 0
     DS(N_("Obsolete (use `unsort')")) },
   { "sort", &c_sort, (A | TWYSH), 0, 1
     DS(N_("Change sorting to: date,from,size,spam,status,subject,thread,to"))},
   { "unsort", &c_unthread, (A | TMSGLST), 0, 0
     DS(N_("Disable sorted or threaded mode")) },
   { "flag", &c_flag, (A | M | TMSGLST), 0, 0
     DS(N_("(Un)Flag <msglist> (for special attention)")) },
   { "unflag", &c_unflag, (A | M | TMSGLST), 0, 0
     DS(N_("(Un)Flag <msglist> (for special attention)")) },
   { "answered", &c_answered, (A | M | TMSGLST), 0, 0
     DS(N_("Mark the given <msglist> as answered")) },
   { "unanswered", &c_unanswered, (A | M | TMSGLST), 0, 0
     DS(N_("Un`answered' <msglist>")) },
   { "draft", &c_draft, (A | M | TMSGLST), 0, 0
     DS(N_("Mark <msglist> as draft")) },
   { "undraft", &c_undraft, (A | M | TMSGLST), 0, 0
     DS(N_("Un`draft' <msglist>")) },
   { "define", &c_define, (M | X | TWYSH), 0, 2
     DS(N_("Define a <macro> or show the currently defined ones")) },
   { "undefine", &c_undefine, (M | X | TWYSH), 1, MAC
     DS(N_("Un`define' all given <macros> (* for all)")) },
   { "unaccount", &c_unaccount, (M | TWYSH), 1, MAC
     DS(N_("Delete all given <accounts> (* for all)")) },
   { "call", &c_call, (M | X | TWYSH), 1, MAC
     DS(N_("Call macro <name>")) },
   { "xcall", &a_go_c_xcall, (M | X | TWYSH), 1, MAC
     DS(N_("Replace currently executing macro with macro <name>")) },
   { "~", &c_call, (M | X | TWYSH), 1, MAC
     DS(N_("Call a macro")) },
   { "call_if", &c_call_if, (M | X | TWYRA), 1, 100
     DS(N_("Call macro <name> if it exists")) },
   { "shift", &c_shift, (M | X | TWYSH), 0, 1
     DS(N_("In a `call'ed macro, shift positional parameters")) },
   { "return", &c_return, (M | X | EM | TWYSH), 0, 2
     DS(N_("Return control [with <return value> [<exit status>]] from macro"))},
   { "move", &c_move, (A | M | TSTRING), 0, 0
     DS(N_("Like `copy', but mark messages for deletion")) },
   { "mv", &c_move, (A | M | TSTRING), 0, 0
     DS(N_("Like `copy', but mark messages for deletion")) },
   { "Move", &c_Move, (A | M | S | TSTRING), 0, 0
     DS(N_("Like `move', but derive filename from first sender")) },
   { "Mv", &c_Move, (A | M | S | TSTRING), 0, 0
     DS(N_("Like `move', but derive filename from first sender")) },
   { "noop", &c_noop, (A | M | TWYSH), 0, 0
     DS(N_("NOOP command if current `file' is accessed via network")) },
   { "collapse", &c_collapse, (A | TMSGLST), 0, 0
     DS(N_("Collapse thread views for <msglist>")) },
   { "uncollapse", &c_uncollapse, (A | TMSGLST), 0, 0
     DS(N_("Uncollapse <msglist> if in threaded view")) },
   { "verify", &c_verify, (A | TMSGLST), 0, 0
     DS(N_("Verify <msglist>")) },
   { "decrypt", &c_decrypt, (A | M | TSTRING), 0, 0
     DS(N_("Like `copy', but decrypt first, if encrypted")) },
   { "Decrypt", &c_Decrypt, (A | M | S | TSTRING), 0, 0
     DS(N_("Like `decrypt', but derive filename from first sender")) },
   { "certsave", &c_certsave, (A | TSTRING), 0, 0
     DS(N_("Save S/MIME certificates of <msglist> to <file>")) },
   { "rename", &c_rename, (M | TWYRA), 0, 2
     DS(N_("Rename <existing-folder> to <new-folder>")) },
   { "remove", &c_remove, (M | TWYRA), 0, MAC
     DS(N_("Remove the named folders")) },
   { "show", &c_show, (A | TMSGLST), 0, MMNDEL
     DS(N_("Like `type', but show raw message content of <msglist>")) },
   { "Show", &c_show, (A | TMSGLST), 0, MMNDEL
     DS(N_("Like `Type', but show raw message content of <msglist>")) },
   { "seen", &c_seen, (A | M | TMSGLST), 0, MMNDEL
     DS(N_("Mark <msglist> as seen")) },
   { "Seen", &c_seen, (A | M | TMSGLST), 0, MMNDEL
     DS(N_("Mark <msglist> as seen")) },
   { "fwdignore", &c_fwdignore, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `headerpick'")) },
   { "fwddiscard", &c_fwdignore, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `headerpick'")) },
   { "fwdretain", &c_fwdretain, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `headerpick'")) },
   { "unfwdignore", &c_unfwdignore, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `unheaderpick'")) },
   { "unfwdretain", &c_unfwdretain, (O | M | TRAWLST), 0, MAC
     DS(N_("Obsoleted by `unheaderpick'")) },
   { "mimetype", &c_mimetype, (M | TWYRA), 0, MAC
     DS(N_("(Load and) show all known MIME types, or define some")) },
   { "unmimetype", &c_unmimetype, (M | TWYRA), 1, MAC
     DS(N_("Delete <type>s (reset, * for all; former reinitializes)")) },
   { "spamrate", &c_spam_rate, (A | M | TMSGLST), 0, 0
     DS(N_("Rate <msglist> via the spam detector")) },
   { "spamham", &c_spam_ham, (A | M | TMSGLST), 0, 0
     DS(N_("Teach the spam detector that <msglist> is ham")) },
   { "spamspam", &c_spam_spam, (A | M | TMSGLST), 0, 0
     DS(N_("Teach the spam detector that <msglist> is spam")) },
   { "spamforget", &c_spam_forget, (A | M | TMSGLST), 0, 0
     DS(N_("Force the spam detector to unlearn <msglist>")) },
   { "spamset", &c_spam_set, (A | M | TMSGLST), 0, 0
     DS(N_("Set the spam flag for each message in <msglist>")) },
   { "spamclear", &c_spam_clear, (A | M | TMSGLST), 0, 0
     DS(N_("Clear the spam flag for each message in <msglist>")) },
   { "cwd", &c_cwd, (M | V | X | TWYSH), 0, 0
     DS(N_("Print current working directory (CWD)")) },
   { "varshow", &c_varshow, (G | M | X | TWYSH), 1, MAC
     DS(N_("Show some informations about the given <variables>")) },
   { "varedit", &c_varedit, (G | I | M | TWYSH), 1, MAC
     DS(N_("Edit the value(s) of (an) variable(s), or create them")) },
   { "vexpr", &c_vexpr, (G | M | V | X | EM | TWYSH), 2, MAC
     DS(N_("Evaluate according to <operator> any <:arguments:>")) },
   { "File", &c_File, (M | T | TWYRA), 0, 1
     DS(N_("Open a new mailbox readonly, or show the current mailbox")) },
   { "Folder", &c_File, (M | T | TWYRA), 0, 1
     DS(N_("Open a new mailbox readonly, or show the current mailbox")) },
   { "mlist", &c_mlist, (M | TWYRA), 0, MAC
     DS(N_("Show all known mailing lists or define some")) },
   { "unmlist", &c_unmlist, (M | TWYRA), 1, MAC
     DS(N_("Un`mlist' <name-list> (* for all)")) },
   { "mlsubscribe", &c_mlsubscribe, (M | TWYRA), 0, MAC
     DS(N_("Show all mailing list subscriptions or define some")) },
   { "unmlsubscribe", &c_unmlsubscribe, (M | TWYRA), 1, MAC
     DS(N_("Un`mlsubscribe' <name-list> (* for all)"))},
   { "Lreply", &c_Lreply, (A | I | R | S | TMSGLST), 0, MMNDEL
     DS(N_("Mailing-list reply to the given <msglist>")) },
   { "errors", &c_errors, (H | I | M | TWYSH), 0, 1
     DS(N_("Either [<show>] or <clear> the error message ring")) },
   { "dotmove", &c_dotmove, (A | TSTRING), 1, 1
     DS(N_("Move the dot up <-> or down <+> by one")) },

   { "commandalias", &a_go_c_alias, (M | X | TWYSH), 0, MAC
     DS(N_("Print/create command <alias> [<command>], or list all aliases")) },
   { "uncommandalias", &a_go_c_unalias, (M | X | TWYSH), 1, MAC
     DS(N_("Delete <command-alias-list> (* for all)")) },
      { "ghost", &a_go_c_alias, (O | M | X | TWYRA), 0, MAC
        DS(N_("Obsoleted by `commandalias'")) },
      { "unghost", &a_go_c_unalias, (O | M | X | TWYRA), 1, MAC
        DS(N_("Obsoleted by `uncommandalias'")) },

   { "eval", &a_go_c_eval, (G | M | X | EM | TWYSH), 1, MAC
     DS(N_("Construct command from <:arguments:>, reuse its $?")) },
   { "localopts", &c_localopts, (H | M | X | TWYSH), 1, 1
     DS(N_("Inside `define' / `account': isolate modifications? <boolean>"))},
   { "read", &a_go_c_read, (G | M | X | EM | TWYSH), 1, MAC
     DS(N_("Read a line from standard input into <variable>(s)")) },
   { "sleep", &c_sleep, (H | M | X | EM | TWYSH), 1, 2
     DS(N_("Sleep for <seconds> [<milliseconds>]"))},
   { "version", &a_go_c_version, (H | M | X | TWYSH), 0, 0
     DS(N_("Show the version and feature set of the program")) },

   { "history", &c_history, (H | I | M | TWYSH), 0, 1
     DS(N_("<show> (default), <clear> or select <NO> from editor history")) },
   { "bind", &c_bind, (M | TSTRING), 1, MAC
     DS(N_("For <context> (base), [<show>] or bind <key[:,key:]> [<:data:>]"))},
   { "unbind", &c_unbind, (M | TSTRING), 2, 2
     DS(N_("Un`bind' <context> <key[:,key:]> (* for all)")) },

   { "netrc", &c_netrc, (M | TWYSH), 0, 1
     DS(N_("[<show>], <load> or <clear> the .netrc cache")) },

   { "charsetalias", &c_charsetalias, (M | TWYSH), 0, MAC
     DS(N_("Define [:<charset> <charset-alias>:]s, or list mappings")) },
   { "uncharsetalias", &c_uncharsetalias, (M | TWYSH), 1, MAC
     DS(N_("Delete <charset-mapping-list> (* for all)")) },

   { "colour", &c_colour, (M | TWYSH), 1, 4
     DS(N_("Show colour settings of <type> (1, 8, 256, all) or define one")) },
   { "uncolour", &c_uncolour, (M | TWYSH), 2, 3
     DS(N_("Un`colour' <type> <mapping> (* for all) [<precondition>]")) },

   { "environ", &c_environ, (G | M | X | TWYSH), 2, MAC
     DS(N_("<link|set|unset> (an) environment <variable>(s)")) },

   { "headerpick", &c_headerpick, (M | TWYSH), 0, MAC
     DS(N_("Header selection: [<context> [<type> [<header-list>]]]"))},
   { "unheaderpick", &c_unheaderpick, (M | TWYSH), 3, MAC
     DS(N_("Header deselection: <context> <type> <header-list>"))},

   { "addrcodec", &c_addrcodec, (G | M | V | X | EM | TRAWDAT), 0, 0
     DS(N_("Email address <[+[+]]e[ncode]|d[ecode]> <rest-of-line>")) },
   { "shcodec", &c_shcodec, (G | M | V | X | EM | TRAWDAT), 0, 0
     DS(N_("Shell quoting: <[+]e[ncode]|d[ecode]> <rest-of-line>")) },
   { "urlcodec", &c_urlcodec, (G | M | V | X | EM | TRAWDAT), 0, 0
     DS(N_("URL percent <[path]e[ncode]|[path]d[ecode]> <rest-of-line>")) },
      { "urlencode", &c_urlencode, (O | G | M | X | TWYRA), 1, MAC
        DS(N_("Obsoleted by `urlcodec'")) },
      { "urldecode", &c_urldecode, (O | G | M | X | TWYRA), 1, MAC
        DS(N_("Obsoleted by `urlcodec'")) },

#ifdef HAVE_MEMORY_DEBUG
   { "memtrace", &c_memtrace, (I | M | TWYSH), 0, 0
     DS(N_("Trace current memory usage afap")) },
#endif
#ifdef HAVE_DEVEL
   { "sigstate", &c_sigstate, (I | M | TWYSH), 0, 0
     DS(N_("Show signal handler states")) },
#endif

#undef MAC
#undef TMSGLST
#undef TNDMLST
#undef TRAWDAT
#  undef TSTRING
#undef TWYSH
#  undef TRAWLST
#  undef TWYRA

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
