/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ (File)Name expansion (globbing).
 *
 * Copyright (c) 2017 - 2024 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_FEXPAND_H
#define mx_FEXPAND_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

enum mx_fexp_mode{
	mx_FEXP_MOST,
	mx_FEXP_SILENT = 1u<<0, /* Do not print but only return errors */
	mx_FEXP_LOCAL = 1u<<1, /* Result must be local file/maildir (glob-expands) */
	mx_FEXP_LOCAL_FILE = 1u<<2, /* ^ local FILE: strips PROTO:// (glob-expands)! */
	mx_FEXP_SHORTCUT = 1u<<3, /* Do expand shortcuts */
	mx_FEXP_NSPECIAL = 1u<<4, /* No %,#,& specials */
	mx_FEXP_NFOLDER = 1u<<5, /* NSPECIAL and no + folder, too */
	mx_FEXP_NSHELL = 1u<<6, /* No shell expansion (does not cover ~/, $VAR) */
	mx_FEXP_NTILDE = 1u<<7, /* No ~/ expansion */
	mx_FEXP_NVAR = 1u<<8, /* No $VAR expansion */
	mx_FEXP_NGLOB = 1u<<9, /* No globbing */

	/* Actually does expand ~/ etc. */
	mx_FEXP_NONE = mx_FEXP_NSPECIAL | mx_FEXP_NFOLDER | mx_FEXP_NVAR | mx_FEXP_NGLOB,
	/* What comes in via variable usually should be expanded */
	mx_FEXP_DEF_FOLDER_VAR = mx_FEXP_SHORTCUT | mx_FEXP_NGLOB,
	mx_FEXP_DEF_FOLDER = mx_FEXP_DEF_FOLDER_VAR | mx_FEXP_NVAR,
	mx_FEXP_DEF_LOCAL_FILE_VAR = mx_FEXP_LOCAL_FILE | mx_FEXP_DEF_FOLDER_VAR,
	mx_FEXP_DEF_LOCAL_FILE = mx_FEXP_DEF_LOCAL_FILE_VAR | mx_FEXP_NVAR
};

/* Evaluate the string given as a new mailbox name. Supported meta characters:
 * . %     for my system mail box
 * . %user for user's system mail box
 * . #     for previous file
 * . &     invoker's mbox file
 * . +file file in folder directory
 * . any shell meta character (except for FEXP_NSHELL)
 * XXX ->name_expand */
EXPORT char *mx_fexpand(char const *name, BITENUM(u32,mx_fexp_mode) fexpm);

/* Like fexpand(), but may instead return an array of strings in the auto-reclaimed result storage; might have
 * expanded the first member before fnmatch, but no further: these come directly via fnmatch! */
EXPORT char **mx_fexpand_multi(char const *name, BITENUM(u32,mx_fexp_mode) fexpm);

#include <su/code-ou.h>
#endif /* mx_FEXPAND_H */
/* s-itt-mode */
