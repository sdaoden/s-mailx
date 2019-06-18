/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Terminal attributes and state.
 *
 * Copyright (c) 2012 - 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_TERMIOS_H
#define mx_TERMIOS_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

enum mx_termios_setup{
   mx_TERMIOS_SETUP_STARTUP,
   mx_TERMIOS_SETUP_TERMSIZE
};

enum mx_termios_cmd{
   mx_TERMIOS_CMD_NORMAL, /* Set "norm"al canonical state (as necessary) */
   mx_TERMIOS_CMD_QUERY, /* Query status, assume that is "norm"al */
   /* Higher values turn terminal state which needs to be reset before exit */
   mx_TERMIOS_CMD_PASSWORD, /* Set password input mode */
   mx_TERMIOS_CMD_RAW, /* Set raw mode, use by-byte input */
   mx_TERMIOS_CMD_RAW_TIMEOUT /* Set raw mode, use (the given) timeout */
};

typedef void (*mx_termios_on_signal)(s32 signal, boole after_reraise);

struct mx_termios_dimension{
   u32 tiosd_height;
   /* .tiosd_height might be deduced via terminal speed, in which case this
    * still is set to the real terminal height */
   u32 tiosd_real_height;
   u32 tiosd_width;
   su_64( u8 tiosd__pad[4]; )
};

/* */
EXPORT_DATA struct mx_termios_dimension mx_termios_dimen;

/* For long iterative output, like `list', tabulator-completion, etc.,
 * determine the screen width that should be used */
#define mx_TERMIOS_WIDTH_OF_LISTS() \
   (mx_termios_dimen.tiosd_width - (mx_termios_dimen.tiosd_width >> 3))

/* Installs signal handlers etc.  Early! */
EXPORT void mx_termios_controller_setup(enum mx_termios_setup what);

/* Install a new signal hook to be run pre/post signal reraise.
 * The old respective handler is returned.
 * When installing, the old handler should be stored so that an entire chain of
 * handlers can be notified by calling the first.
 * The old former handler should be reinstalled when done.
 * xxx Of course this is primitive, yet sufficient for now */
EXPORT mx_termios_on_signal mx_termios_set_on_signal(mx_termios_on_signal hdl);

/* For _RAW and _RAW_TIMEOUT a1 describes VMIN and VTIME, respectively */
EXPORT boole mx_termios_cmd(enum mx_termios_cmd cmd, uz a1);
#define mx_termios_cmdx(CMD) mx_termios_cmd(CMD, 0)

#include <su/code-ou.h>
#endif /* mx_TERMIOS_H */
/* s-it-mode */
