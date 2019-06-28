/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Terminal attributes and state.
 *
 * Copyright (c) 2019 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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

enum mx_termios_cmd{
   /* Throw away the entire stack, and restore normal terminal state.
    * The outermost level will be regulary shutdown, as via POP.
    * Further bits may not be set (this is 0) */
   mx_TERMIOS_CMD_RESET,
   /* Create a (POPable) environment, as necessary change to the given mode.
    * An environment carries the terminal mode as well as a possibly installed
    * on_state_change hook; if such environment is reentered, the state change
    * hook gets called to resume after the mode has been reestablished.
    * Likewise, if it is left, it gets called to suspend first.
    * XXX Any state change requires this, only RAW and RAW_TIMEOUT may be
    * XXX switched back and forth on the same level (otherwise state_change
    * XXX needs cmd argument, plus plus plus) */
   mx_TERMIOS_CMD_PUSH = 1u<<0,
   /* Pop stack and restore the terminal setting active before.
    * If a hook is installed, it will be called first.
    * If a mode is given, debug version will assert the stack top matches.
    * Further bits are ignored */
   mx_TERMIOS_CMD_POP = 1u<<1,
   mx__TERMIOS_CMD_CTL_MASK = mx_TERMIOS_CMD_PUSH | mx_TERMIOS_CMD_POP,
   mx_TERMIOS_CMD_NORMAL = 1u<<3, /* Normal canonical mode */
   mx_TERMIOS_CMD_PASSWORD = 2u<<3, /* Password input mode */
   mx_TERMIOS_CMD_RAW = 3u<<3, /* Raw mode, use by-(the given-)byte(s) input */
   mx_TERMIOS_CMD_RAW_TIMEOUT = 4u<<3, /* Raw mode, use (the given) timeout */
   mx_TERMIOS_CMD_HANDS_OFF = 5u<<3, /* We do not own the terminal */
   mx__TERMIOS_CMD_ACT_MASK = 7u<<3
};

enum mx_termios_setup{
   mx_TERMIOS_SETUP_STARTUP,
   mx_TERMIOS_SETUP_TERMSIZE
};

enum mx_termios_state_change{
   mx_TERMIOS_STATE_SUSPEND = 1u<<0, /* Need to suspend terminal state */
   mx_TERMIOS_STATE_RESUME = 1u<<1, /* Need to resume terminal state */
   mx_TERMIOS_STATE_SIGNAL = 1u<<2, /* Change was caused by a !job signal */
   /* This (final) _SUSPEND is caused by the environment being popped */
   mx_TERMIOS_STATE_POP = 1u<<3
};

/* tiossc is termios_state_change bitmix.
 * signal is only meaningful when _STATE_SIGNAL is set */
typedef void (*mx_termios_on_state_change)(u32 tiossc, s32 signal);

struct mx_termios_dimension{
   u32 tiosd_height;
   /* .tiosd_height might be deduced via terminal speed, in which case this
    * still is set to the real terminal height */
   u32 tiosd_real_height;
   u32 tiosd_width;
   /* .tiosd_width might be reduces deduced by one if we have no termcap
    * support or if the terminal cannot write in the last column (without
    * wrapping), in which case this still is set to the real terminal width */
   u32 tiosd_real_width;
};

/* */
EXPORT_DATA struct mx_termios_dimension mx_termios_dimen;

/* For long iterative output, like `list', tabulator-completion, etc.,
 * determine the screen width that should be used */
#define mx_TERMIOS_WIDTH_OF_LISTS() \
   (mx_termios_dimen.tiosd_width - (mx_termios_dimen.tiosd_width >> 3))

/* Installs signal handlers etc.  Early! */
EXPORT void mx_termios_controller_setup(enum mx_termios_setup what);

/* Install a new state change hook for the current environment,
 * and return the old handler.
 * May not be used in the top-, nor in a HANDS_OFF level */
EXPORT mx_termios_on_state_change mx_termios_on_state_change_set(
      mx_termios_on_state_change hdl);

/* tiosc is a bitmix of mx_termios_cmd values.
 * For _RAW and _RAW_TIMEOUT a1 describes VMIN and VTIME, respectively */
EXPORT boole mx_termios_cmd(u32 tiosc, uz a1);
#define mx_termios_cmdx(CMD) mx_termios_cmd(CMD, 0)

#include <su/code-ou.h>
#endif /* mx_TERMIOS_H */
/* s-it-mode */
