/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `filetype'.
 *
 * Copyright (c) 2017 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_CMD_FILETYPE_H
#define mx_CMD_FILETYPE_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_filetype;

struct mx_filetype{
   char const *ft_ext_dat; /* Extension this handles, without first period */
   uz ft_ext_len;
   char const *ft_load_dat; /* And the load and save command strings */
   uz ft_load_len;
   char const *ft_save_dat;
   uz ft_save_len;
};

/* `(un)?filetype' */
EXPORT int c_filetype(void *vp);
EXPORT int c_unfiletype(void *vp);

/* Whether the non-existing file has a handable "equivalent", to be checked by
 * iterating over all established extensions and trying the resulting
 * concatenated filename; a set res_or_nil will be filled on success (data must
 * be copied out) */
EXPORT boole mx_filetype_trial(struct mx_filetype *res_or_nil,
      char const *file);

/* Whether (the extension of) file is known; a set res_or_nil will be filled
 * on success (data must be copied out) */
EXPORT boole mx_filetype_exists(struct mx_filetype *res_or_nil,
      char const *file);

#include <su/code-ou.h>
#endif /* mx_CMD_FILETYPE_H */
/* s-it-mode */
