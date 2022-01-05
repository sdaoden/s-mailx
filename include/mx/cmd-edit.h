/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `edit', `visual'.
 *
 * Copyright (c) 2012 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
EXPORT int c_edit(void *v);

/* Invoke the visual editor on a message list */
EXPORT int c_visual(void *v);

/* Run an editor on either size bytes of the file fp (or until EOF if size is
 * negative) or on the message mp, and return a new file or NIL on error of if
 * the user did not perform any edits (not possible in pipe mode).
 * For now we ASSERT that mp==NIL if hp!=NIL, treating this as a special call
 * from within compose mode.
 * Signals must be handled by the caller.
 * viored is 'e' for $EDITOR, 'v' for $VISUAL, or '|' for child_run(), in
 * which case pipecmd must have been given */
EXPORT FILE *n_run_editor(FILE *fp, off_t size, int viored, boole readonly,
      struct header *hp, struct message *mp, enum sendaction action,
      n_sighdl_t oldint, char const *pipecmd);

#include <su/code-ou.h>
#endif /* mx_CMD_EDIT_H */
/* s-it-mode */
