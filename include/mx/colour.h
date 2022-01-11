/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `(un)?colour' commands, and anything working with it.
 *
 * Copyright (c) 2014 - 2022 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_COLOUR_H
#define mx_COLOUR_H

#include <mx/nail.h>
#ifdef mx_HAVE_COLOUR

#include "mx/go.h" /* For mx_COLOUR_IS_ACTIVE() TODO */

#define mx_HEADER
#include <su/code-in.h>

struct mx_colour_env;

/* Injection, if mx_HAVE_COLOUR */
#define mx_COLOUR(X) X

/* We do have several contexts of colour IDs; since only one of them can be
 * active at any given time let's share the value range */
enum mx_colour_ctx{
   mx_COLOUR_CTX_SUM,
   mx_COLOUR_CTX_VIEW,
   mx_COLOUR_CTX_MLE,
   mx__COLOUR_CTX_MAX1
};

enum mx_colour_id{
   /* Header summary */
   mx_COLOUR_ID_SUM_DOTMARK = 0,
   mx_COLOUR_ID_SUM_HEADER,
   mx_COLOUR_ID_SUM_THREAD,

   /* Message display */
   mx_COLOUR_ID_VIEW_FROM_ = 0,
   mx_COLOUR_ID_VIEW_HEADER,
   mx_COLOUR_ID_VIEW_MSGINFO,
   mx_COLOUR_ID_VIEW_PARTINFO,

   /* Mailx-Line-Editor */
   mx_COLOUR_ID_MLE_POSITION = 0,
   mx_COLOUR_ID_MLE_PROMPT,
   mx_COLOUR_ID_MLE_ERROR,

   mx__COLOUR_IDS = mx_COLOUR_ID_VIEW_PARTINFO + 1
};

/* Some non-string colour precondition constants, let us call them tags */
#define mx_COLOUR_TAG_SUM_DOT R(char*,-2)
#define mx_COLOUR_TAG_SUM_OLDER R(char*,-3)

enum mx_colour_get_flags{
   mx_COLOUR_GET_FORCED = 1u<<0 /* Act even if COLOUR_IS_ACTIVE() is false */
};

struct mx_colour_env{
   struct mx_colour_env *ce_last;
   boole ce_enabled; /* Colour enabled on this level */
   u8 ce_ctx; /* enum mx_colour_ctx */
   u8 ce__pad[6];
   FILE *ce_outfp;
   struct a_colour_map *ce_current; /* Active colour or NIL */
};

struct mx_colour_pen;

/* `(un)?colour' */
EXPORT int c_colour(void *v);
EXPORT int c_uncolour(void *v);

/* TODO All the colour (pen etc. ) interfaces below have to vanish.
 * TODO What we need here is a series of query functions which take context,
 * TODO like a message* for the _VIEW_ series, and which return a 64-bit
 * TODO flag carrier which returns font-attributes as well as foreground and
 * TODO background colours (at least 24-bit each).
 * TODO And the actual drawing stuff is up to the backend, maybe in termios,
 * TODO or better termcap, or in ui-str.  I do not know */

/* An execution context is teared down, and it finds to have a colour stack.
 * Signals are blocked */
EXPORT void mx_colour_stack_del(struct mx_go_data_ctx *gdcp);

/* We want coloured output (in this autorec memory cycle), if fp is given, the
 * reset sequence will be written as necessary by _stack_del() env_gut() will
 * reset() as necessary if fp is not NIL */
EXPORT void mx_colour_env_create(enum mx_colour_ctx cctx, FILE *fp);
EXPORT void mx_colour_env_gut(void);

/* Putting anything (for pens: including NIL) resets current state first */
EXPORT void mx_colour_put(enum mx_colour_id cid, char const *ctag);
EXPORT void mx_colour_reset(void);

/* Of course temporary only and may return NIL.  Does not affect state! */
EXPORT struct str const *mx_colour_reset_to_str(void);

/* A pen is bound to its environment and automatically reclaimed; it may be
 * NULL but that can be used anyway for simplicity.
 * This includes pen_to_str() -- which doesn't affect state! */
EXPORT struct mx_colour_pen *mx_colour_pen_create(enum mx_colour_id cid,
                           char const *ctag);
EXPORT void mx_colour_pen_put(struct mx_colour_pen *self);

/* Get an escape sequence (or NIL, if self is, or no colour there is) */
EXPORT struct str const *mx_colour_pen_to_str(struct mx_colour_pen *self);

/* Get terminal reset control sequence (or NIL if no colour there is).
 * The return value is "volatile" to colour change commands */
EXPORT struct str const *mx_colour_get_reset_cseq(u32 get_flags);

/* Just get the pen for the given combination, or NIL.
 * The return value is "volatile" to colour change commands */
EXPORT struct mx_colour_pen *mx_colour_get_pen(u32 get_flags,
      enum mx_colour_ctx cctx, enum mx_colour_id cid, char const *ctag);

/* Get terminal control sequence (or NIL, if self is, or no colour there is) */
EXPORT struct str const *mx_colour_pen_get_cseq(
      struct mx_colour_pen const *self);

#include <su/code-ou.h>
#else
# define mx_COLOUR(X)
#endif /* mx_HAVE_COLOUR */
#endif /* mx_COLOUR_H */
/* s-it-mode */
