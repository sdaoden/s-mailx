/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Random string creation.
 *
 * Copyright (c) 2015 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_RANDOM_H
#define mx_RANDOM_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

/* Get a (pseudo) random string of *len* bytes, _not_ counting the NUL
 * terminator, the second returns an n_autorec_alloc()ed buffer.
 * If su_STATE_REPRODUCIBLE and reprocnt_or_nil not NIL then we produce
 * a reproducible string by using and managing that counter instead */
EXPORT char *mx_random_create_buf(char *dat, uz len, u32 *reprocnt_or_nil);
EXPORT char *mx_random_create_cp(uz len, u32 *reprocnt_or_nil);

/* Any non-TLS su_random hook */
#if su_RANDOM_SEED == su_RANDOM_SEED_HOOK && mx_RANDOM_SEED_HOOK != 3
EXPORT boole mx_random_hook(void **cookie, void *buf, uz len);
#endif

#include <su/code-ou.h>
#endif /* mx_RANDOM_H */
/* s-it-mode */
