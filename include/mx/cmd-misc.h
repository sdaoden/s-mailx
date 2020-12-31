/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Miscellaneous user commands, like `echo', `pwd', etc.
 *
 * Copyright (c) 2012 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CMD_MISC_H
#define mx_CMD_MISC_H

#include <su/code.h>

#define mx_HEADER
#include <su/code-in.h>

/* `!': process a shell escape by saving signals, ignoring signals and sh -c */
EXPORT int c_shell(void *vp);

/* `shell': fork an interactive shell */
EXPORT int c_dosh(void *vp);

/* `cwd': print user's working directory */
EXPORT int c_cwd(void *vp);

/* `chdir': change user's working directory */
EXPORT int c_chdir(void *vp);

/* `echo' series: expand file names like echo (to stdout/stderr, with/out
 * trailing newline) */
EXPORT int c_echo(void *vp);
EXPORT int c_echoerr(void *vp);
EXPORT int c_echon(void *vp);
EXPORT int c_echoerrn(void *vp);

/* `read', `readsh' */
EXPORT int c_read(void *vp);
EXPORT int c_readsh(void *vp);

/* `readall' */
EXPORT int c_readall(void *vp);

/* `version' */
EXPORT int c_version(void *vp);

#include <su/code-ou.h>
#endif /* mx_CMD_MISC_H */
/* s-it-mode */
