/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `edit', `visual'.
 *
 * Copyright (c) 2012 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
 * SPDX-License-Identifier: ISC
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef mx_CMD_EDIT_H
#define mx_CMD_EDIT_H

#include <mx/nail.h>

#include <mx/sigs.h>

#define mx_HEADER
#include <su/code-in.h>

/* Edit a message list */
EXPORT int c_edit(void *vp);

/* Invoke the visual editor on a message list */
EXPORT int c_visual(void *vp);

/* Run an editor on either cnt bytes of the file fp_or_nil (or until EOF if cnt is negative), or on the message
 * mp_or_nil, and return a new file or NIL on error if the user did not perform any edits (not possible in pipe mode).
 * For now we ASSERT that mp_or_nil==NIL if hp_or_nil!=NIL, treating this as a special call from within compose mode.
 * Signals must be handled by the caller.
 * viored 'e'=$EDITOR, 'v'=$VISUAL, or '|' for child_run(), in which case pipecmd_or_nil must have been given */
EXPORT FILE *mx_run_editor(int viored, boole rdonly, enum sendaction action, n_sighdl_t oldint,
		FILE *fp_or_nil, s64 cnt, struct header *hp_or_nil, struct message *mp_or_nil, char const *pipecmd_or_nil);

#include <su/code-ou.h>
#endif /* mx_CMD_EDIT_H */
/* s-itt-mode */
