/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Define all of the command names and bindings.
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2013 Steffen "Daode" Nurpmeso <sdaoden@users.sf.net>.
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

#include "rcv.h"
#include "extern.h"

#ifdef USE_DOCSTRINGS
# define DS(ID,S)	, ID, S
#else
# define DS(ID,S)
#endif

struct cmd const cmdtab[] = {
   { "next", next, (A | NDMLIST), 0, MMNDEL
     DS(355, "Goes to the next message (-list) and prints it")
   },
   { "alias", group, (M | RAWLIST), 0, 1000
     DS(304, "Show all or the specified alias(es), or (re)define one")
   },
   { "print", type, (A | MSGLIST), 0, MMNDEL
     DS(361, "Type each message of <message-list> on the terminal")
   },
   { "type", type, (A | MSGLIST), 0, MMNDEL
     DS(361, "Type each message of <message-list> on the terminal")
   },
   { "Type", Type, (A | MSGLIST), 0, MMNDEL
     DS(360, "Like \"print\", but prints all headers and parts")
   },
   { "Print", Type, (A | MSGLIST), 0, MMNDEL
     DS(360, "Like \"print\", but prints all headers and parts")
   },
   { "visual", visual, (A | I | MSGLIST), 0, MMNORM
     DS(326, "Edit <message-list>")
   },
   { "top", top, (A | MSGLIST), 0, MMNDEL
     DS(385, "Print top few lines of <message-list>")
   },
   { "touch", stouch, (A | W | MSGLIST), 0, MMNDEL
     DS(386, "Mark <message-list> for saving in *mbox*")
   },
   { "preserve", preserve, (A | W | MSGLIST), 0, MMNDEL
     DS(344, "Save <message-list> in system mailbox instead of *mbox*")
   },
   { "delete", delete, (A | W | P | MSGLIST), 0, MMNDEL
     DS(320, "Delete <message-list>")
   },
   { "dp", deltype, (A | W | MSGLIST), 0, MMNDEL
     DS(323, "Delete the current message, then print the next")
   },
   { "dt", deltype, (A | W | MSGLIST), 0, MMNDEL
     DS(323, "Delete the current message, then print the next")
   },
   { "undelete", undeletecmd, (A | P | MSGLIST), MDELETED,MMNDEL
     DS(392, "Un\"delete\" <message-list>")
   },
   { "unset", unset, (M | RAWLIST), 1, 1000
     DS(402, "Unset <option-list>")
   },
   { "mail", sendmail, (R | M | I | STRLIST), 0, 0
     DS(351, "Compose mail; recipients may be given as arguments")
   },
   { "Mail", Sendmail, (R | M | I | STRLIST), 0, 0
     DS(350, "Like \"mail\", but derive filename from first recipient")
   },
   { "mbox", mboxit, (A | W | MSGLIST), 0, 0
     DS(352, "Indicate that <message-list> is to be stored in *mbox*")
   },
   { "more", more, (A | MSGLIST), 0, MMNDEL
     DS(410, "Like \"type\"/\"print\", put print \\f between messages")
   },
   { "page", more, (A | MSGLIST), 0, MMNDEL
     DS(410, "Like \"type\"/\"print\", put print \\f between messages")
   },
   { "More", More, (A | MSGLIST), 0, MMNDEL
     DS(411, "Like \"Type\"/\"Print\", put print \\f between messages")
   },
   { "Page", More, (A | MSGLIST), 0, MMNDEL
     DS(411, "Like \"Type\"/\"Print\", put print \\f between messages")
   },
   { "unread", unread, (A | MSGLIST), 0, MMNDEL
     DS(356, "Mark <message-list> as not being read")
   },
   { "Unread", unread, (A | MSGLIST), 0, MMNDEL
     DS(356, "Mark <message-list> as not being read")
   },
   { "new", unread, (A | MSGLIST), 0, MMNDEL
     DS(356, "Mark <message-list> as not being read")
   },
   { "New", unread, (A | MSGLIST), 0, MMNDEL
     DS(356, "Mark <message-list> as not being read")
   },
   { "!", shell, (I | STRLIST), 0, 0
     DS(412, "Execute <shell-command>")
   },
   { "copy", copycmd, (A | M | STRLIST), 0, 0
     DS(314, "Copy <message-list>, but don't mark them for deletion")
   },
   { "Copy", Copycmd, (A | M | STRLIST), 0, 0
     DS(315, "Like \"copy\", but derive filename from first sender")
   },
   { "chdir", schdir, (M | RAWLIST), 0, 1
     DS(309, "Change CWD to the specified/the login directory")
   },
   { "cd", schdir, (M | RAWLIST), 0, 1
     DS(309, "Change CWD to the specified/the login directory")
   },
   { "save", save, (A | STRLIST), 0, 0
     DS(371, "Append <message-list> to <file>")
   },
   { "Save", Save, (A | STRLIST), 0, 0
     DS(372, "Like \"save\", but derive filename from first sender")
   },
   { "source", csource, (M | RAWLIST), 1, 1
     DS(383, "Read commands from <file>")
   },
   { "set", set, (M | RAWLIST), 0, 1000
     DS(376, "Print all variables, or set variables to argument(s)")
   },
   { "shell", dosh, (I | NOLIST), 0, 0
     DS(378, "Invoke an interactive shell")
   },
   { "version", pversion, (M | NOLIST), 0, 0
     DS(413, "Print the MUA version")
   },
   { "unalias", ungroup, (M | RAWLIST), 0, 1000
     DS(387, "Un\"alias\" <name-list>")
   },
   { "write", cwrite, (A | STRLIST), 0, 0
     DS(406, "Write (append) to <file>")
   },
   { "from", from, (A | MSGLIST), 0, MMNORM
     DS(338, "Show message headers of <message-list>")
   },
   { "file", cfile, (T | M | RAWLIST), 0, 1
     DS(329, "Switch to new or show the current mail file or folder")
   },
   { "followup", followup, (A | R | I | MSGLIST), 0, MMNDEL
     DS(333, "Like \"respond\", but derive filename from first sender")
   },
   { "followupall", followupall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(334, "Like \"respond\", but derive filename from first sender")
   },
   { "followupsender", followupsender, (A | R | I | MSGLIST), 0, MMNDEL
     DS(335, "Like \"Followup\", but always respond to the sender only")
   },
   { "folder", cfile, (T | M | RAWLIST), 0, 1
     DS(329, "Switch to new or show the current mail file or folder")
   },
   { "folders", folders, (T | M | RAWLIST), 0, 1
     DS(332, "List folders (below given folder)")
   },
   { "z", scroll, (A | M | STRLIST), 0, 0
     DS(407, "Scroll to next/previous window of headers")
   },
   { "Z", Scroll, (A | M | STRLIST), 0, 0
     DS(408, "Like \"z\", but continues to the next flagged message")
   },
   { "headers", headers, (A | MSGLIST), 0, MMNDEL
     DS(342, "Show the current(/last/next) 18-message group of headers")
   },
   { "help", help, (M | RAWLIST), 0, 1
     DS(343, "Show command help (for the given one)")
   },
   { "?", help, (M | RAWLIST), 0, 1
     DS(343, "Show command help (for the given one)")
   },
   { "=", pdot, (A | NOLIST), 0, 0
     DS(409, "Show current message number")
   },
   { "Reply", Respond, (A | R | I | MSGLIST), 0, MMNDEL
     DS(368, "Reply to originator, exclusively")
   },
   { "Respond", Respond, (A | R | I | MSGLIST), 0, MMNDEL
     DS(368, "Reply to originator, exclusively")
   },
   { "Followup", Followup, (A | R | I | MSGLIST), 0, MMNDEL
     DS(332, "Like \"Respond\", but derive filename from first sender")
   },
   { "reply", respond, (A | R | I | MSGLIST), 0, MMNDEL
     DS(369, "Reply to originator and recipients of <message-list>")
   },
   { "replyall", respondall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(369, "Reply to originator and recipients of <message-list>")
   },
   { "replysender", respondsender, (A | R | I | MSGLIST), 0, MMNDEL
     DS(368, "Reply to originator, exclusively")
   },
   { "respond", respond, (A | R | I | MSGLIST), 0, MMNDEL
     DS(369, "Reply to originators and recipients of <message-list>")
   },
   { "respondall", respondall, (A | R | I | MSGLIST), 0, MMNDEL
     DS(369, "Reply to originators and recipients of <message-list>")
   },
   { "respondsender", respondsender, (A | R | I | MSGLIST),0, MMNDEL
     DS(368, "Reply to originator, exclusively")
   },
   { "Resend", Resendcmd, (A | R | STRLIST), 0, MMNDEL
     DS(365, "Like \"resend\", but don't add Resent-* headers")
   },
   { "Redirect", Resendcmd, (A | R | STRLIST), 0, MMNDEL
     DS(365, "Like \"resend\", but don't add Resent-* headers")
   },
   { "resend", resendcmd, (A | R | STRLIST), 0, MMNDEL
     DS(364, "Resend <message-list> to <user>, add Resent-* headers")
   },
   { "redirect", resendcmd, (A | R | STRLIST), 0, MMNDEL
     DS(364, "Resend <message-list> to <user>, add Resent-* headers")
   },
   { "Forward", Forwardcmd, (A | R | STRLIST), 0, MMNDEL
     DS(337, "Like \"forward\", but derive filename from <address>")
   },
   { "Fwd", Forwardcmd, (A | R | STRLIST), 0, MMNDEL
     DS(337, "Like \"forward\", but derive filename from <address>")
   },
   { "forward", forwardcmd, (A | R | STRLIST), 0, MMNDEL
     DS(336, "Forward <message> to <address>")
   },
   { "fwd", forwardcmd, (A | R | STRLIST), 0, MMNDEL
     DS(336, "Forward <message> to <address>")
   },
   { "edit", editor, (A | I | MSGLIST), 0, MMNORM
     DS(326, "Edit <message-list>")
   },
   { "echo", echo, (M | ECHOLIST), 0, 1000
     DS(325, "Echo given arguments")
   },
   { "quit", quitcmd, NOLIST, 0, 0
     DS(363, "Terminate session, saving messages as necessary")
   },
   { "list", pcmdlist, (M | NOLIST), 0, 0
     DS(349, "List all available commands")
   },
   { "xit", rexit, (M | NOLIST), 0, 0
     DS(328, "Immediate return to the shell without saving")
   },
   { "exit", rexit, (M | NOLIST), 0, 0
     DS(328, "Immediate return to the shell without saving")
   },
   { "pipe", pipecmd, (A | STRLIST), 0, MMNDEL
     DS(359, "Pipe <message-list> to <command>")
   },
   { " | ", pipecmd, (A | STRLIST), 0, MMNDEL
     DS(359, "Pipe <message-list> to <command>")
   },
   { "Pipe", Pipecmd, (A | STRLIST), 0, MMNDEL
     DS(358, "Like \"pipe\", but pipes all headers and parts")
   },
   { "size", messize, (A | MSGLIST), 0, MMNDEL
     DS(381, "Show size in characters for <message-list>")
   },
   { "hold", preserve, (A | W | MSGLIST), 0, MMNDEL
     DS(344, "Save <message-list> in system mailbox instead of *mbox*")
   },
   { "if", ifcmd, (F | M | RAWLIST), 1, 1
     DS(327, "Part of the if .. then .. endif statement")
   },
   { "else", elsecmd, (F | M | RAWLIST), 0, 0
     DS(327, "Part of the if .. then .. endif statement")
   },
   { "endif", endifcmd, (F | M | RAWLIST), 0, 0
     DS(327, "Part of the if .. then .. endif statement")
   },
   { "alternates", alternates, (M | RAWLIST), 0, 1000
     DS(305, "Show or define an alternate list for the invoking user")
   },
   { "ignore", igfield, (M | RAWLIST), 0, 1000
     DS(321, "Add header fields to ignored LIST), or show that list")
   },
   { "discard", igfield, (M | RAWLIST), 0, 1000
     DS(321, "Add header fields to ignored LIST), or show that list")
   },
   { "retain", retfield, (M | RAWLIST), 0, 1000
     DS(370, "Add header fields to retained LIST), or show that list")
   },
   { "saveignore", saveigfield, (M | RAWLIST), 0, 1000
     DS(373, "Is to \"save\" what \"ignore\" is to \"type\"/\"print\"")
   },
   { "savediscard",saveigfield, (M | RAWLIST), 0, 1000
     DS(373, "Is to \"save\" what \"ignore\" is to \"type\"/\"print\"")
   },
   { "saveretain", saveretfield, (M | RAWLIST), 0, 1000
     DS(374, "Is to \"save\" what \"retain\" is to \"type\"/\"print\"")
   },
   { "unignore", unignore, (M | RAWLIST), 0, 1000
     DS(396, "Un\"ignore\" <header-fields>")
   },
   { "unretain", unretain, (M | RAWLIST), 0, 1000
     DS(399, "Un\"retain\" <header-fields>")
   },
   { "unsaveignore", unsaveignore, (M | RAWLIST), 0, 1000
     DS(400, "Un\"saveignore\" <header-fields>")
   },
   { "unsaveretain", unsaveretain, (M | RAWLIST), 0, 1000
     DS(401, "Un\"saveretain\" <header-fields>")
   },
   { "inc", newmail, (A | T | NOLIST), 0, 0
     DS(346, "Check for new mail in current folder")
   },
   { "newmail", newmail, (A | T | NOLIST), 0, 0
     DS(346, "Check for new mail in current folder")
   },
   { "shortcut", shortcut, (M | RAWLIST), 0, 1000
     DS(379, "Define a <shortcut> and <expansion>, or list shortcuts")
   },
   { "unshortcut", unshortcut, (M | RAWLIST), 0, 1000
     DS(403, "Delete <shortcut-list>")
   },
   { "imap", imap_imap, (A | STRLIST), 0, 1000
     DS(345, "Send command strings directly to the IMAP server")
   },
   { "account", account, (M | RAWLIST), 0, 1000
     DS(303, "Creates, selects or lists an email account")
   },
   { "thread", thread, (A | MSGLIST), 0, 0
     DS(384, "Create threaded view of current \"folder\"")
   },
   { "unthread", unthread, (A | MSGLIST), 0, 0
     DS(404, "Disable sorted or threaded mode")
   },
   { "online", cconnect, (A | NOLIST), 0, 0
     DS(314, "If disconnected, connect to IMAP mailbox")
   },
   { "connect", cconnect, (A | NOLIST), 0, 0
     DS(314, "If disconnected, connect to IMAP mailbox")
   },
   { "disconnect", cdisconnect, (A | NDMLIST), 0, 0
     DS(322, "If connected, disconnect from IMAP mailbox")
   },
   { "sort", sort, (A | RAWLIST), 0, 1
     DS(382, "Change sorting criteria (and addressing modes)")
   },
   { "unsort", unthread, (A | MSGLIST), 0, 0
     DS(404, "Disable sorted or threaded mode")
   },
   { "cache", ccache, (A | MSGLIST), 0, 0
     DS(307, "Read specified <message list> into the IMAP cache")
   },
   { "flag", cflag, (A | M | MSGLIST), 0, 0
     DS(330, "(Un)Flag <message-list> (for special attention)")
   },
   { "unflag", cunflag, (A | M | MSGLIST), 0, 0
     DS(330, "(Un)Flag <message-list> (for special attention)")
   },
   { "answered", canswered, (A | M | MSGLIST), 0, 0
     DS(306, "Mark the given <message list> as \"answered\"")
   },
   { "unanswered", cunanswered, (A | M | MSGLIST), 0, 0
     DS(388, "Un\"answered\" <message-list>")
   },
   { "draft", cdraft, (A | M | MSGLIST), 0, 0
     DS(324, "Mark <message-list> as draft")
   },
   { "undraft", cundraft, (A | M | MSGLIST), 0, 0
     DS(389, "Un\"draft\" <message-list>")
   },
   { "define", cdefine, (M | RAWLIST), 0, 2
     DS(319, "Define a macro")
   },
   { "defines", cdefines, (M | RAWLIST), 0, 0
     DS(320, "Show all defined macros including their content")
   },
   { "undef", cundef, (M | RAWLIST), 0, 1000
     DS(391, "Un\"define\" all <macros>")
   },
   { "call", ccall, (M | RAWLIST), 0, 1
     DS(308, "Call a macro")
   },
   { "~", ccall, (M | RAWLIST), 0, 1
     DS(308, "Call a macro")
   },
   { "move", cmove, (A | M | STRLIST), 0, 0
     DS(353, "Like \"copy\", but mark messages for deletion")
   },
   { "mv", cmove, (A | M | STRLIST), 0, 0
     DS(353, "Like \"copy\", but mark messages for deletion")
   },
   { "Move", cMove, (A | M | STRLIST), 0, 0
     DS(354, "Like \"move\", but derive filename from first sender")
   },
   { "Mv", cMove, (A | M | STRLIST), 0, 0
     DS(354, "Like \"move\", but derive filename from first sender")
   },
   { "noop", cnoop, (A | M | RAWLIST), 0, 0
     DS(357, "NOOP command if IMAP or POP folder; else noop")
   },
   { "collapse", ccollapse, (A | MSGLIST), 0, 0
     DS(312, "Collapse thread views for <message-list>")
   },
   { "uncollapse", cuncollapse, (A | MSGLIST), 0, 0
     DS(390, "Uncollapse <message-list> if in threaded view")
   },
   { "verify", cverify, (A | MSGLIST), 0, 0
     DS(405, "Verify <message-list>")
   },
   { "decrypt", cdecrypt, (A | M | STRLIST), 0, 0
     DS(316, "Like \"copy\", but decrypt first, if encrypted")
   },
   { "Decrypt", cDecrypt, (A | M | STRLIST), 0, 0
     DS(317, "Like \"decrypt\", but derive filename from first sender")
   },
   { "certsave", ccertsave, (A | STRLIST), 0, 0
     DS(310, "Save S/MIME certificates of <message-list> to <file>")
   },
   { "rename", crename, (M | RAWLIST), 0, 2
     DS(367, "Rename <existing-folder> to <new-folder>")
   },
   { "remove", cremove, (M | RAWLIST), 0, 1000
     DS(366, "Remove the named folders")
   },
   { "show", show, (A | MSGLIST), 0, MMNDEL
     DS(380, "Like \"print\", but show raw message content")
   },
   { "Show", show, (A | MSGLIST), 0, MMNDEL
     DS(380, "Like \"print\", but show raw message content")
   },
   { "seen", seen, (A | M | MSGLIST), 0, MMNDEL
     DS(377, "Mark <message-list> as seen")
   },
   { "Seen", seen, (A | M | MSGLIST), 0, MMNDEL
     DS(377, "Mark <message-list> as seen")
   },
   { "fwdignore", fwdigfield, (M | RAWLIST), 0, 1000
     DS(339, "Which header fields are to be ignored with \"forward\"")
   },
   { "fwddiscard", fwdigfield, (M | RAWLIST), 0, 1000
     DS(339, "Which header fields are to be ignored with \"forward\"")
   },
   { "fwdretain", fwdretfield, (M | RAWLIST), 0, 1000
     DS(340, "Which header fields have to be retained with \"forward\"")
   },
   { "unfwdignore", unfwdignore, (M | RAWLIST), 0, 1000
     DS(393, "Un\"fwdignore\" <header-fields>")
   },
   { "unfwdretain", unfwdretain, (M | RAWLIST), 0, 1000
     DS(394, "Un\"fwdretain\" <header-fields>")
   },
/*	{ "Header", Header, STRLIST), 0, 1000	*/
   { "mimetypes", cmimetypes, (M | RAWLIST), 0, 1000
     DS(418, "Either <show> (default) or <clear> the mime.types cache")
   },
   { "spamrate", cspam_rate, (A | M | R | MSGLIST), 0, 0
     DS(419, "Rate <message-list> via the spam detector")
   },
   { "spamham", cspam_ham, (A | M | R | MSGLIST), 0, 0
     DS(420, "Teach the spam detector that <message-list> is ham")
   },
   { "spamspam", cspam_spam, (A | M | R | MSGLIST), 0, 0
     DS(421, "Teach the spam detector that <message-list> is spam")
   },
   { "spamforget", cspam_forget, (A | M | R | MSGLIST), 0, 0
     DS(422, "Force the spam detector to \"unlearn\" <message-list>")
   },
   { "spamset", cspam_set, (A | M | MSGLIST), 0, 0
     DS(423, "Set the spam flag for each message in <message-list>")
   },
   { "spamclear", cspam_clear, (A | M | MSGLIST), 0, 0
     DS(424, "Clear the spam flag for each message in <message-list>")
   },
#ifdef HAVE_ASSERTS
   { "core", core, (M | NOLIST), 0, 0
     DS(414, "Produce a core dump (ouch!)")
   },
   { "clobber", clobber, (M | RAWLIST), 0, 1
     DS(415, "Globber <number> 512 byte blocks on the stack")
   },
   { "sstats", sstats, (M | NOLIST), 0, 0
     DS(416, "Print statistics about the auto-reclaimed string store")
   },
   { "smemtrace", smemtrace, (M | NOLIST), 0, 0
     DS(417, "Trace current memory usage afap")
   },
#endif
   { NULL, NULL, 0, 0, 0 DS(0, "") }
};

/* vim:set fenc=utf-8:s-it-mode */
