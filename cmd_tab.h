/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ This header is included by ./lex.c and defines the command array content.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2014 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#ifdef HAVE_DOCSTRINGS
# define DS(ID,S)    , ID, S
#else
# define DS(ID,S)
#endif

   /* Note: the first command in here may NOT expand to an unsupported one! */
   { "next", &c_next, (A | NDMLIST), 0, MMNDEL
     DS(355, "Goes to the next message (-list) and prints it") },
   { "alias", &c_group, (M | RAWLIST), 0, 1000
     DS(304, "Show all or the specified alias(es), or (re)define one") },
   { "print", &c_type, (A | MSGLIST), 0, MMNDEL
     DS(361, "Type each message of <message-list> on the terminal") },
   { "type", &c_type, (A | MSGLIST), 0, MMNDEL
     DS(361, "Type each message of <message-list> on the terminal") },
   { "Type", &c_Type, (A | MSGLIST), 0, MMNDEL
     DS(360, "Like \"print\", but prints all headers and parts") },
   { "Print", &c_Type, (A | MSGLIST), 0, MMNDEL
     DS(360, "Like \"print\", but prints all headers and parts") },
   { "visual", &c_visual, (A | I | MSGLIST), 0, MMNORM
     DS(326, "Edit <message-list>") },
   { "top", &c_top, (A | MSGLIST), 0, MMNDEL
     DS(385, "Print top few lines of <message-list>") },
   { "touch", &c_stouch, (A | W | MSGLIST), 0, MMNDEL
     DS(386, "Mark <message-list> for saving in *mbox*") },
   { "preserve", &c_preserve, (A | W | MSGLIST), 0, MMNDEL
     DS(344, "Save <message-list> in system mailbox instead of *mbox*") },
   { "delete", &c_delete, (A | W | P | MSGLIST), 0, MMNDEL
     DS(320, "Delete <message-list>") },
   { "dp", &c_deltype, (A | W | MSGLIST), 0, MMNDEL
     DS(323, "Delete the current message, then print the next") },
   { "dt", &c_deltype, (A | W | MSGLIST), 0, MMNDEL
     DS(323, "Delete the current message, then print the next") },
   { "undelete", &c_undelete, (A | P | MSGLIST), MDELETED,MMNDEL
     DS(392, "Un\"delete\" <message-list>") },
   { "unset", &c_unset, (M | RAWLIST), 1, 1000
     DS(402, "Unset <option-list>") },
   { "mail", &c_sendmail, (R | M | I | STRLIST), 0, 0
     DS(351, "Compose mail; recipients may be given as arguments") },
   { "Mail", &c_Sendmail, (R | M | I | STRLIST), 0, 0
     DS(350, "Like \"mail\", but derive filename from first recipient") },
   { "mbox", &c_mboxit, (A | W | MSGLIST), 0, 0
     DS(352, "Indicate that <message-list> is to be stored in *mbox*") },
   { "more", &c_more, (A | MSGLIST), 0, MMNDEL
     DS(410, "Like \"type\"/\"print\", put print \\f between messages") },
   { "page", &c_more, (A | MSGLIST), 0, MMNDEL
     DS(410, "Like \"type\"/\"print\", put print \\f between messages") },
   { "More", &c_More, (A | MSGLIST), 0, MMNDEL
     DS(411, "Like \"Type\"/\"Print\", put print \\f between messages") },
   { "Page", &c_More, (A | MSGLIST), 0, MMNDEL
     DS(411, "Like \"Type\"/\"Print\", put print \\f between messages") },
   { "unread", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(356, "Mark <message-list> as not being read") },
   { "Unread", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(356, "Mark <message-list> as not being read") },
   { "new", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(356, "Mark <message-list> as not being read") },
   { "New", &c_unread, (A | MSGLIST), 0, MMNDEL
     DS(356, "Mark <message-list> as not being read") },
   { "!", &c_shell, (I | STRLIST), 0, 0
     DS(412, "Execute <shell-command>") },
   { "copy", &c_copy, (A | M | STRLIST), 0, 0
     DS(314, "Copy <message-list>, but don't mark them for deletion") },
   { "Copy", &c_Copy, (A | M | STRLIST), 0, 0
     DS(315, "Like \"copy\", but derive filename from first sender") },
   { "chdir", &c_chdir, (M | RAWLIST), 0, 1
     DS(309, "Change CWD to the specified/the login directory") },
   { "cd", &c_chdir, (M | RAWLIST), 0, 1
     DS(309, "Change CWD to the specified/the login directory") },
   { "save", &c_save, (A | STRLIST), 0, 0
     DS(371, "Append <message-list> to <file>") },
   { "Save", &c_Save, (A | STRLIST), 0, 0
     DS(372, "Like \"save\", but derive filename from first sender") },
   { "source", &c_source, (M | RAWLIST), 1, 1
     DS(383, "Read commands from <file>") },
   { "set", &c_set, (M | RAWLIST), 0, 1000
     DS(376, "Print all variables, or set variables to argument(s)") },
   { "shell", &c_dosh, (I | NOLIST), 0, 0
     DS(378, "Invoke an interactive shell") },
   { "unalias", &c_ungroup, (M | RAWLIST), 0, 1000
     DS(387, "Un\"alias\" <name-list>") },
   { "write", &c_write, (A | STRLIST), 0, 0
     DS(406, "Write (append) to <file>") },
   { "from", &c_from, (A | MSGLIST), 0, MMNORM
     DS(338, "Show message headers of <message-list>") },
   { "file", &c_file, (T | M | RAWLIST), 0, 1
     DS(329, "Switch to new or show the current mail file or folder") },
   { "followup", &c_followup, (A | R | I | MSGLIST), 0, MMNDEL
     DS(333, "Like \"respond\", but derive filename from first sender") },
   { "followupall", &c_followupall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(334, "Like \"respond\", but derive filename from first sender") },
   { "followupsender", &c_followupsender, (A | R | I | MSGLIST), 0, MMNDEL
     DS(335, "Like \"Followup\", but always respond to the sender only") },
   { "folder", &c_file, (T | M | RAWLIST), 0, 1
     DS(329, "Switch to new or show the current mail file or folder") },
   { "folders", &c_folders, (T | M | RAWLIST), 0, 1
     DS(332, "List folders (below given folder)") },
   { "z", &c_scroll, (A | M | STRLIST), 0, 0
     DS(407, "Scroll to next/previous window of headers") },
   { "Z", &c_Scroll, (A | M | STRLIST), 0, 0
     DS(408, "Like \"z\", but continues to the next flagged message") },
   { "headers", &c_headers, (A | MSGLIST), 0, MMNDEL
     DS(342, "Show the current(/last/next) 18-message group of headers") },
   { "help", &c_help, (/*H |*/ M | RAWLIST), 0, 1
     DS(343, "Show command help (for the given one)") },
   { "?", &c_help, (H | M | RAWLIST), 0, 1
     DS(343, "Show command help (for the given one)") },
   { "=", &c_pdot, (A | NOLIST), 0, 0
     DS(409, "Show current message number") },
   { "Reply", &c_Respond, (A | R | I | MSGLIST), 0, MMNDEL
     DS(368, "Reply to originator, exclusively") },
   { "Respond", &c_Respond, (A | R | I | MSGLIST), 0, MMNDEL
     DS(368, "Reply to originator, exclusively") },
   { "Followup", &c_Followup, (A | R | I | MSGLIST), 0, MMNDEL
     DS(332, "Like \"Respond\", but derive filename from first sender") },
   { "reply", &c_respond, (A | R | I | MSGLIST), 0, MMNDEL
     DS(369, "Reply to originator and recipients of <message-list>") },
   { "replyall", &c_respondall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(369, "Reply to originator and recipients of <message-list>") },
   { "replysender", &c_respondsender, (A | R | I | MSGLIST), 0, MMNDEL
     DS(368, "Reply to originator, exclusively") },
   { "respond", &c_respond, (A | R | I | MSGLIST), 0, MMNDEL
     DS(369, "Reply to originators and recipients of <message-list>") },
   { "respondall", &c_respondall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(369, "Reply to originators and recipients of <message-list>") },
   { "respondsender", &c_respondsender, (A | R | I | MSGLIST),0, MMNDEL
     DS(368, "Reply to originator, exclusively") },
   { "Resend", &c_Resend, (A | R | STRLIST), 0, MMNDEL
     DS(365, "Like \"resend\", but don't add Resent-* headers") },
   { "Redirect", &c_Resend, (A | R | STRLIST), 0, MMNDEL
     DS(365, "Like \"resend\", but don't add Resent-* headers") },
   { "resend", &c_resend, (A | R | STRLIST), 0, MMNDEL
     DS(364, "Resend <message-list> to <user>, add Resent-* headers") },
   { "redirect", &c_resend, (A | R | STRLIST), 0, MMNDEL
     DS(364, "Resend <message-list> to <user>, add Resent-* headers") },
   { "Forward", &c_Forward, (A | R | STRLIST), 0, MMNDEL
     DS(337, "Like \"forward\", but derive filename from <address>") },
   { "Fwd", &c_Forward, (A | R | STRLIST), 0, MMNDEL
     DS(337, "Like \"forward\", but derive filename from <address>") },
   { "forward", &c_forward, (A | R | STRLIST), 0, MMNDEL
     DS(336, "Forward <message> to <address>") },
   { "fwd", &c_forward, (A | R | STRLIST), 0, MMNDEL
     DS(336, "Forward <message> to <address>") },
   { "edit", &c_editor, (A | I | MSGLIST), 0, MMNORM
     DS(326, "Edit <message-list>") },
   { "echo", &c_echo, (M | ECHOLIST), 0, 1000
     DS(325, "Echo given arguments") },
   { "quit", &c_quit, NOLIST, 0, 0
     DS(363, "Terminate session, saving messages as necessary") },
   { "list", &_pcmdlist, (M | NOLIST), 0, 0
     DS(349, "List all available commands") },
   { "xit", &c_rexit, (M | NOLIST), 0, 0
     DS(328, "Immediate return to the shell without saving") },
   { "exit", &c_rexit, (M | NOLIST), 0, 0
     DS(328, "Immediate return to the shell without saving") },
   { "pipe", &c_pipe, (A | STRLIST), 0, MMNDEL
     DS(359, "Pipe <message-list> to <command>") },
   { "|", &c_pipe, (A | STRLIST), 0, MMNDEL
     DS(359, "Pipe <message-list> to <command>") },
   { "Pipe", &c_Pipe, (A | STRLIST), 0, MMNDEL
     DS(358, "Like \"pipe\", but pipes all headers and parts") },
   { "size", &c_messize, (A | MSGLIST), 0, MMNDEL
     DS(381, "Show size in characters for <message-list>") },
   { "hold", &c_preserve, (A | W | MSGLIST), 0, MMNDEL
     DS(344, "Save <message-list> in system mailbox instead of *mbox*") },
   { "if", &c_if, (F | M | RAWLIST), 1, 3
     DS(327, "Part of the if .. then .. endif statement") },
   { "else", &c_else, (F | M | RAWLIST), 0, 0
     DS(327, "Part of the if .. then .. endif statement") },
   { "endif", &c_endif, (F | M | RAWLIST), 0, 0
     DS(327, "Part of the if .. then .. endif statement") },
   { "alternates", &c_alternates, (M | RAWLIST), 0, 1000
     DS(305, "Show or define an alternate list for the invoking user") },
   { "ignore", &c_igfield, (M | RAWLIST), 0, 1000
     DS(321, "Add header fields to ignored LIST), or show that list") },
   { "discard", &c_igfield, (M | RAWLIST), 0, 1000
     DS(321, "Add header fields to ignored LIST), or show that list") },
   { "retain", &c_retfield, (M | RAWLIST), 0, 1000
     DS(370, "Add header fields to retained LIST), or show that list") },
   { "saveignore", &c_saveigfield, (M | RAWLIST), 0, 1000
     DS(373, "Is to \"save\" what \"ignore\" is to \"type\"/\"print\"") },
   { "savediscard", &c_saveigfield, (M | RAWLIST), 0, 1000
     DS(373, "Is to \"save\" what \"ignore\" is to \"type\"/\"print\"") },
   { "saveretain", &c_saveretfield, (M | RAWLIST), 0, 1000
     DS(374, "Is to \"save\" what \"retain\" is to \"type\"/\"print\"") },
   { "unignore", &c_unignore, (M | RAWLIST), 0, 1000
     DS(396, "Un\"ignore\" <header-fields>") },
   { "unretain", &c_unretain, (M | RAWLIST), 0, 1000
     DS(399, "Un\"retain\" <header-fields>") },
   { "unsaveignore", &c_unsaveignore, (M | RAWLIST), 0, 1000
     DS(400, "Un\"saveignore\" <header-fields>") },
   { "unsaveretain", &c_unsaveretain, (M | RAWLIST), 0, 1000
     DS(401, "Un\"saveretain\" <header-fields>") },
   { "inc", &c_newmail, (A | T | NOLIST), 0, 0
     DS(346, "Check for new mail in current folder") },
   { "newmail", &c_newmail, (A | T | NOLIST), 0, 0
     DS(346, "Check for new mail in current folder") },
   { "shortcut", &c_shortcut, (M | RAWLIST), 0, 1000
     DS(379, "Define a <shortcut> and <expansion>, or list shortcuts") },
   { "unshortcut", &c_unshortcut, (M | RAWLIST), 0, 1000
     DS(403, "Delete <shortcut-list>") },
   { "imap", &c_imap_imap, (A | STRLIST), 0, 1000
     DS(345, "Send command strings directly to the IMAP server") },
   { "account", &c_account, (M | RAWLIST), 0, 1000
     DS(303, "Creates, selects or lists an email account") },
   { "thread", &c_thread, (A | MSGLIST), 0, 0
     DS(384, "Create threaded view of current \"folder\"") },
   { "unthread", &c_unthread, (A | MSGLIST), 0, 0
     DS(404, "Disable sorted or threaded mode") },
   { "online", &c_connect, (A | NOLIST), 0, 0
     DS(314, "If disconnected, connect to IMAP mailbox") },
   { "connect", &c_connect, (A | NOLIST), 0, 0
     DS(314, "If disconnected, connect to IMAP mailbox") },
   { "disconnect", &c_disconnect, (A | NDMLIST), 0, 0
     DS(322, "If connected, disconnect from IMAP mailbox") },
   { "sort", &c_sort, (A | RAWLIST), 0, 1
     DS(382, "Change sorting criteria (and addressing modes)") },
   { "unsort", &c_unthread, (A | MSGLIST), 0, 0
     DS(404, "Disable sorted or threaded mode") },
   { "cache", &c_cache, (A | MSGLIST), 0, 0
     DS(307, "Read specified <message list> into the IMAP cache") },
   { "flag", &c_flag, (A | M | MSGLIST), 0, 0
     DS(330, "(Un)Flag <message-list> (for special attention)") },
   { "unflag", &c_unflag, (A | M | MSGLIST), 0, 0
     DS(330, "(Un)Flag <message-list> (for special attention)") },
   { "answered", &c_answered, (A | M | MSGLIST), 0, 0
     DS(306, "Mark the given <message list> as \"answered\"") },
   { "unanswered", &c_unanswered, (A | M | MSGLIST), 0, 0
     DS(388, "Un\"answered\" <message-list>") },
   { "draft", &c_draft, (A | M | MSGLIST), 0, 0
     DS(324, "Mark <message-list> as draft") },
   { "undraft", &c_undraft, (A | M | MSGLIST), 0, 0
     DS(389, "Un\"draft\" <message-list>") },
   { "define", &c_define, (M | RAWLIST), 0, 2
     DS(319, "Define a macro") },
   { "defines", &c_defines, (I | M | RAWLIST), 0, 0
     DS(320, "Show all defined macros including their content") },
   { "undef", &c_undef, (M | RAWLIST), 0, 1000
     DS(391, "Un\"define\" all <macros>") },
   { "call", &c_call, (M | RAWLIST), 0, 1
     DS(308, "Call a macro") },
   { "~", &c_call, (M | RAWLIST), 0, 1
     DS(308, "Call a macro") },
   { "move", &c_move, (A | M | STRLIST), 0, 0
     DS(353, "Like \"copy\", but mark messages for deletion") },
   { "mv", &c_move, (A | M | STRLIST), 0, 0
     DS(353, "Like \"copy\", but mark messages for deletion") },
   { "Move", &c_Move, (A | M | STRLIST), 0, 0
     DS(354, "Like \"move\", but derive filename from first sender") },
   { "Mv", &c_Move, (A | M | STRLIST), 0, 0
     DS(354, "Like \"move\", but derive filename from first sender") },
   { "noop", &c_noop, (A | M | RAWLIST), 0, 0
     DS(357, "NOOP command if IMAP or POP folder; else noop") },
   { "collapse", &c_collapse, (A | MSGLIST), 0, 0
     DS(312, "Collapse thread views for <message-list>") },
   { "uncollapse", &c_uncollapse, (A | MSGLIST), 0, 0
     DS(390, "Uncollapse <message-list> if in threaded view") },
   { "verify", &c_verify, (A | MSGLIST), 0, 0
     DS(405, "Verify <message-list>") },
   { "decrypt", &c_decrypt, (A | M | STRLIST), 0, 0
     DS(316, "Like \"copy\", but decrypt first, if encrypted") },
   { "Decrypt", &c_Decrypt, (A | M | STRLIST), 0, 0
     DS(317, "Like \"decrypt\", but derive filename from first sender") },
   { "certsave", &c_certsave, (A | STRLIST), 0, 0
     DS(310, "Save S/MIME certificates of <message-list> to <file>") },
   { "rename", &c_rename, (M | RAWLIST), 0, 2
     DS(367, "Rename <existing-folder> to <new-folder>") },
   { "remove", &c_remove, (M | RAWLIST), 0, 1000
     DS(366, "Remove the named folders") },
   { "show", &c_show, (A | MSGLIST), 0, MMNDEL
     DS(380, "Like \"print\", but show raw message content") },
   { "Show", &c_show, (A | MSGLIST), 0, MMNDEL
     DS(380, "Like \"print\", but show raw message content") },
   { "seen", &c_seen, (A | M | MSGLIST), 0, MMNDEL
     DS(377, "Mark <message-list> as seen") },
   { "Seen", &c_seen, (A | M | MSGLIST), 0, MMNDEL
     DS(377, "Mark <message-list> as seen") },
   { "fwdignore", &c_fwdigfield, (M | RAWLIST), 0, 1000
     DS(339, "Which header fields are to be ignored with \"forward\"") },
   { "fwddiscard", &c_fwdigfield, (M | RAWLIST), 0, 1000
     DS(339, "Which header fields are to be ignored with \"forward\"") },
   { "fwdretain", &c_fwdretfield, (M | RAWLIST), 0, 1000
     DS(340, "Which header fields have to be retained with \"forward\"") },
   { "unfwdignore", &c_unfwdignore, (M | RAWLIST), 0, 1000
     DS(393, "Un\"fwdignore\" <header-fields>") },
   { "unfwdretain", &c_unfwdretain, (M | RAWLIST), 0, 1000
     DS(394, "Un\"fwdretain\" <header-fields>") },
   { "mimetypes", &c_mimetypes, (M | RAWLIST), 0, 1000
     DS(418, "Either <show> (default) or <clear> the mime.types cache") },
   { "spamrate", &c_spam_rate, (A | M | R | MSGLIST), 0, 0
     DS(419, "Rate <message-list> via the spam detector") },
   { "spamham", &c_spam_ham, (A | M | R | MSGLIST), 0, 0
     DS(420, "Teach the spam detector that <message-list> is ham") },
   { "spamspam", &c_spam_spam, (A | M | R | MSGLIST), 0, 0
     DS(421, "Teach the spam detector that <message-list> is spam") },
   { "spamforget", &c_spam_forget, (A | M | R | MSGLIST), 0, 0
     DS(422, "Force the spam detector to \"unlearn\" <message-list>") },
   { "spamset", &c_spam_set, (A | M | MSGLIST), 0, 0
     DS(423, "Set the spam flag for each message in <message-list>") },
   { "spamclear", &c_spam_clear, (A | M | MSGLIST), 0, 0
     DS(424, "Clear the spam flag for each message in <message-list>") },
   { "ghost", &_ghost, (M | RAWLIST), 0, 2
     DS(425, "Define a <ghost> of <command>, or list all ghosts") },
   { "unghost", &_unghost, (M | RAWLIST), 1, 1000
     DS(426, "Delete <ghost-list>") },
   { "localopts", &c_localopts, (M | RAWLIST), 1, 1
     DS(427, "Inside `define' / `account': insulate modifications? <0> / <1>")},
   { "cwd", &c_cwd, (M | NOLIST), 0, 0
     DS(428, "Print current working directory (CWD)") },
   { "pwd", &c_cwd, (M | NOLIST), 0, 0
     DS(428, "Print current working directory (CWD)") },
   { "var-inspect", &c_var_inspect, (M | RAWLIST), 1, 1000
     DS(430, "Print some informations on the given <variables>") },
   { "features", &_features, (M | NOLIST), 0, 0
     DS(429, "Show features that are compiled into the MUA") },
   { "version", &_version, (M | NOLIST), 0, 0
     DS(413, "Print the MUA version") },
#ifdef HAVE_HISTORY
   { "history", &c_history, (H | I | M | V | RAWLIST), 0, 1
     DS(431, "<show> (default), <clear> or select <NO> from editor history") },
#endif
#ifdef HAVE_DEBUG
   { "sstats", &c_sstats, (I | M | NOLIST), 0, 0
     DS(416, "Print statistics about the auto-reclaimed string store") },
   { "smemtrace", &c_smemtrace, (I | M | NOLIST), 0, 0
     DS(417, "Trace current memory usage afap") },
#endif
   { NULL, NULL, 0, 0, 0 DS(0, "") }

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

/* vim:set fenc=utf-8:s-it-mode */
