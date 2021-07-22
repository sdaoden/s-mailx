/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ Implementation of tty.h -- M(ailx) L(ine) E(ditor).
 *@ Because we have (had) multiple line-editor implementations, including our
 *@ own MLE, change layout a bit and place them one after the other.
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
#define su_FILE tty_mle
#define mx_SOURCE
#define mx_SOURCE_TTY_MLE

#ifndef mx_HAVE_AMALGAMATION
# include "mx/nail.h"
#endif

#include <su/mem.h>

#ifdef mx_HAVE_MLE
# include <su/cs.h>
# include <su/mem-bag.h>
# include <su/utf.h>

# include <su/icodec.h>

# ifdef mx_HAVE_HISTORY
#  include <su/sort.h>
# endif
# ifdef mx_HAVE_REGEX
#  include <su/re.h>
# endif
#endif

#include "mx/file-streams.h"

#ifdef mx_HAVE_MLE
# include "mx/cmd.h"
# include "mx/compat.h"
# include "mx/file-locks.h"
# include "mx/go.h"
# include "mx/sigs.h"
# include "mx/termcap.h"
# include "mx/termios.h"
# include "mx/ui-str.h"

# ifdef mx_HAVE_COLOUR
#  include "mx/colour.h"
# endif
#endif

#include "mx/tty.h"
/* TODO fake */
/*#define NYDPROF_ENABLE*/
/*#define NYD_ENABLE*/
/*#define NYD2_ENABLE*/
#include "su/code-in.h"

FILE *mx_tty_fp; /* Our terminal output TODO input channel */

/*
 * MLE: the Mailx-Line-Editor, our homebrew editor
 *
 * Only used in interactive mode.
 * TODO . This code should be split in funs/raw input/bind modules.
 * TODO . Multiline visual via CUPS; adjust srch-pos0 docu, then.
 * TODO . We work with wide characters, but not for buffer takeovers and
 * TODO   cell2save()ings.  This should be changed.  For the former the buffer
 * TODO   thus needs to be converted to wide first, and then simply be fed in.
 * TODO . We repaint too much.  To overcome this use the same approach that my
 * TODO   terminal library uses, add a true "virtual screen line" that stores
 * TODO   the actually visible content, keep a notion of "first modified slot"
 * TODO   and "last modified slot" (including "unknown" and "any" specials),
 * TODO   update that virtual instead, then synchronize what has truly changed.
 * TODO   I.e., add an indirection layer.
 * TODO .. This also has an effect on our signal hook (as long as this codebase
 * TODO    uses SA_RESTART), which currently does a tremendous amount of work.
 * TODO    With double-buffer, it could simply write through the prepared one.
 * TODO . No BIDI support.
 */
#ifdef mx_HAVE_MLE
/* To avoid memory leaks etc. with the current codebase that simply longjmp(3)s
 * we are forced to use the very same buffer--the one that is passed through to
 * us from the outside--to store anything we need, i.e., a "struct cell[]", and
 * convert that on-the-fly back to the plain char* result once we are done.
 * To simplify our live, use savestr() buffers for all other needed memory */

# ifdef mx_HAVE_KEY_BINDINGS
   /* We have a chicken-and-egg problem with `bind' and our termcap layer,
    * because we may not initialize the latter automatically to allow users to
    * specify *termcap-disable*, and let it mean exactly that.
    * On the other hand users can be expected to use `bind' in resources.
    * Therefore bindings which involve termcap/terminfo sequences, and which
    * are defined before n_PSO_STARTED_CONFIG signals usability of
    * termcap/terminfo, will be (partially) delayed until tty_init() is called.
    * And we preallocate space for the expansion of the resolved capability */
#  define a_TTY_BIND_CAPNAME_MAX 15
#  define a_TTY_BIND_CAPEXP_ROUNDUP 16

CTAV(IS_POW2(a_TTY_BIND_CAPEXP_ROUNDUP));
CTA(a_TTY_BIND_CAPEXP_ROUNDUP <= S8_MAX / 2, "Variable must fit in 6-bit");
CTA(a_TTY_BIND_CAPEXP_ROUNDUP >= 8, "Variable too small");

   /* Bind lookup trees organized in (wchar_t indexed) hashmaps */
#  define a_TTY_PRIME 0xBu
# endif /* mx_HAVE_KEY_BINDINGS */

# ifdef mx_HAVE_HISTORY
   /* The first line of the history file is used as a marker after >v14.9.6 */
#  define a_TTY_HIST_MARKER "@s-mailx history v2"
# endif

/* The maximum size (of a_tty_cell's) in a line */
# define a_TTY_LINE_MAX S32_MAX

/* (Some more CTAs around) */
CTA(a_TTY_LINE_MAX <= S32_MAX,
   "a_TTY_LINE_MAX larger than S32_MAX, but the MLE uses 32-bit arithmetic");

/* When shall the visual screen be scrolled, in % of usable screen width */
# define a_TTY_SCROLL_MARGIN_LEFT 15
# define a_TTY_SCROLL_MARGIN_RIGHT 10

/* fexpand() flags for expand-on-tab */
# define a_TTY_TAB_FEXP_FL (FEXP_NOPROTO | FEXP_FULL | FEXP_SILENT)

/* Columns to ripoff: position indicator.
 * Should be >= 4 to dig the position indicator that we place (if there is
 * sufficient space) */
# define a_TTY_WIDTH_RIPOFF 4

/* The implementation of the MLE functions always exists, and is based upon
 * the a_TTY_BIND_FUN_* constants, so most of this enum is always necessary */
enum a_tty_bind_flags{
# ifdef mx_HAVE_KEY_BINDINGS
   a_TTY_BIND_RESOLVE = 1u<<8, /* Term cap. yet needs to be resolved */
   a_TTY_BIND_DEFUNCT = 1u<<9, /* Unicode/term cap. used but not avail. */
   a_TTY__BIND_MASK = a_TTY_BIND_RESOLVE | a_TTY_BIND_DEFUNCT,
   /* MLE fun assigned to a one-byte-sequence: this may be used for special
    * key-sequence bypass processing */
   a_TTY_BIND_MLE1CNTRL = 1u<<10,
   a_TTY_BIND_NOCOMMIT = 1u<<11, /* Expansion shall be editable */
# endif

   /* MLE internal commands */
   a_TTY_BIND_FUN_INTERNAL = 1u<<15,
   a_TTY__BIND_FUN_SHIFT = 16u,
   a_TTY__BIND_FUN_SHIFTMAX = 24u,
   a_TTY__BIND_FUN_MASK = ((1u << a_TTY__BIND_FUN_SHIFTMAX) - 1) &
         ~((1u << a_TTY__BIND_FUN_SHIFT) - 1),
# define a_TTY_BIND_FUN_REDUCE(X) \
   ((S(u32,X) & a_TTY__BIND_FUN_MASK) >> a_TTY__BIND_FUN_SHIFT)
# define a_TTY_BIND_FUN_EXPAND(X) \
   ((S(u32,X) & (a_TTY__BIND_FUN_MASK >> a_TTY__BIND_FUN_SHIFT)) << \
      a_TTY__BIND_FUN_SHIFT)
# undef a_X
# define a_X(N,I)\
   a_TTY_BIND_FUN_ ## N = a_TTY_BIND_FUN_EXPAND(I),

   a_X(BELL, 0)
   a_X(GO_BWD, 1) a_X(GO_FWD, 2)
   a_X(GO_WORD_BWD, 3) a_X(GO_WORD_FWD, 4)
   a_X(GO_SCREEN_BWD, 5) a_X(GO_SCREEN_FWD, 6)
   a_X(GO_HOME, 7) a_X(GO_END, 8)
   a_X(DEL_BWD, 9) a_X(DEL_FWD, 10)
   a_X(SNARF_WORD_BWD, 11) a_X(SNARF_WORD_FWD, 12)
   a_X(SNARF_END, 13) a_X(SNARF_LINE, 14)
   a_X(HIST_BWD, 15) a_X(HIST_FWD, 16)
   a_X(HIST_SRCH_BWD, 17) a_X(HIST_SRCH_FWD, 18)
   a_X(REPAINT, 19)
   a_X(QUOTE_RNDTRIP, 20)
   a_X(PROMPT_CHAR, 21)
   a_X(COMPLETE, 22)
   a_X(PASTE, 23)
   a_X(CLEAR_SCREEN, 24)

   a_X(RAISE_INT, 25)
   a_X(RAISE_QUIT, 26)
   a_X(RAISE_TSTP, 27)

   a_X(CANCEL, 28)
   a_X(RESET, 29)
   a_X(FULLRESET, 30)

   a_X(COMMIT, 31) /* Must be last one (else adjust CTAs)! */
# undef a_X

   a_TTY__BIND_LAST = 1u<<28
};
# ifdef mx_HAVE_KEY_BINDINGS
CTA(S(u32,a_TTY_BIND_RESOLVE) >= S(u32,mx__GO_INPUT_CTX_MAX1),
   "Bit carrier lower boundary must be raised to avoid value sharing");
# endif
CTA(a_TTY_BIND_FUN_EXPAND(a_TTY_BIND_FUN_COMMIT) <
      (1u << a_TTY__BIND_FUN_SHIFTMAX),
   "Bit carrier range must be expanded to represent necessary bits");
CTA(a_TTY__BIND_LAST >= (1u << a_TTY__BIND_FUN_SHIFTMAX),
   "Bit carrier upper boundary must be raised to avoid value sharing");
CTA(UCMP(64, a_TTY__BIND_LAST, <=, S32_MAX),
   "Flag bits excess storage datatype" /* And we need one bit free */);

enum a_tty_config_flags{
   a_TTY_CONF_QUOTE_RNDTRIP = 1u<<0, /* mle-quote-rndtrip default-on */
   a_TTY_CONF_SRCH_CASE = 1u<<1, /* mle-hist-srch-* case-insensitive */
   a_TTY_CONF_SRCH_POS0 = 1u<<2, /* *mle-hist-srch* moves cursor to pos 0 */
   a_TTY_CONF_SRCH_ANY = 1u<<3, /* mle-hist-srch-* "matches any substring" */
   a_TTY_CONF_SRCH_REGEX = 1u<<4, /* mle-hist-srch-* uses regex */

   a_TTY__CONF_LAST = a_TTY_CONF_SRCH_REGEX,
   a_TTY__CONF_MASK = (a_TTY__CONF_LAST << 1) - 1
};
CTA(a_TTY__CONF_LAST <= U16_MAX, "Flag bits excess storage datatype");

enum a_tty_fun_status{
   a_TTY_FUN_STATUS_OK, /* Worked, next character */
   a_TTY_FUN_STATUS_COMMIT, /* Line done */
   a_TTY_FUN_STATUS_RESTART, /* Complete restart, reset multibyte etc. */
   a_TTY_FUN_STATUS_END /* End, return EOF */
};

# ifdef mx_HAVE_HISTORY
enum a_tty_hist_flags{
   a_TTY_HIST_CTX_DEFAULT = mx_GO_INPUT_CTX_DEFAULT,
   a_TTY_HIST_CTX_COMPOSE = mx_GO_INPUT_CTX_COMPOSE,
   a_TTY_HIST_CTX_MASK = mx__GO_INPUT_CTX_MASK,
   /* Cannot use enum mx_go_input_flags for the rest, need to stay in 8-bit */
   a_TTY_HIST_GABBY = 1u<<7,
   a_TTY_HIST__MAX = a_TTY_HIST_GABBY
};
CTA(a_TTY_HIST_CTX_MASK < a_TTY_HIST_GABBY, "Enumeration value overlap");
# endif

# ifdef mx_HAVE_KEY_BINDINGS
enum a_tty_term_timeout_mode{
   a_TTY_TTM_NONE,
   a_TTY_TTM_MBSEQ,
   a_TTY_TTM_KEY,
   a_TTY_TTM_KEY_AFTER_MBSEQ
};
# endif

enum a_tty_visual_flags{
   a_TTY_VF_NONE,
   a_TTY_VF_MOD_CURSOR = 1u<<0, /* Cursor moved */
   a_TTY_VF_MOD_CONTENT = 1u<<1, /* Content modified */
   a_TTY_VF_MOD_DIRTY = 1u<<2, /* Needs complete repaint */
   a_TTY_VF_MOD_SINGLE = 1u<<3, /* TODO Drop if indirection as above comes */
   a_TTY_VF_REFRESH = a_TTY_VF_MOD_DIRTY | a_TTY_VF_MOD_CURSOR |
         a_TTY_VF_MOD_CONTENT | a_TTY_VF_MOD_SINGLE,
   a_TTY_VF_BELL = 1u<<8, /* Ring the bell */
   a_TTY_VF_MAXWIDTH_POS0 = 1u<<9, /* srch-pos0 (one screen + adj cursor) */
   a_TTY_VF_SYNC = 1u<<10, /* Flush/Sync I/O channel */

   a_TTY_VF_ALL_MASK = a_TTY_VF_REFRESH | a_TTY_VF_BELL |
         a_TTY_VF_MAXWIDTH_POS0 | a_TTY_VF_SYNC,
   a_TTY__VF_LAST = a_TTY_VF_SYNC
};
CTA(a_TTY__VF_LAST <= U16_MAX, "Flag bits excess storage datatype");

# ifdef mx_HAVE_KEY_BINDINGS
struct a_tty_bind_ctx{
   struct a_tty_bind_ctx *tbc_next;
   char *tbc_seq; /* quence as given (poss. re-quoted), in .tb__buf */
   char *tbc_exp; /* ansion, in .tb__buf */
   /* The .tbc_seq'uence with any terminal capabilities resolved; in fact an
    * array of structures, the first entry of which is {s32 buf_len_iscap;}
    * where the signed bit indicates whether the buffer is a resolved terminal
    * capability instead of a (possibly multibyte) character.  In .tbc__buf */
   char *tbc_cnv;
   u32 tbc_seq_len;
   u32 tbc_exp_len;
   u32 tbc_cnv_len;
   u32 tbc_flags;
   char tbc__buf[VFIELD_SIZE(0)];
};
# endif /* mx_HAVE_KEY_BINDINGS */

struct a_tty_bind_builtin_tuple{
   boole tbbt_iskey;  /* Whether this is a control key; else termcap query */
   char tbbt_ckey; /* Control code */
   u16 tbbt_query; /* enum mx_termcap_query (instead) */
   char tbbt_exp[12]; /* String or [0]=NUL/[1]=BIND_FUN_REDUCE() */
};
CTA(mx__TERMCAP_QUERY_MAX1 <= U16_MAX,
   "Enumeration cannot be stored in datatype");

# ifdef mx_HAVE_KEY_BINDINGS
struct a_tty_bind_parse_ctx{
   char const *tbpc_cmd; /* Command which parses */
   char const *tbpc_in_seq; /* In: key sequence */
   struct str tbpc_exp; /* In/Out: expansion (or NIL) */
   struct a_tty_bind_ctx *tbpc_tbcp; /* Out: if yet existent */
   struct a_tty_bind_ctx *tbpc_ltbcp; /* Out: the one before .tbpc_tbcp */
   char *tbpc_seq; /* Out: normalized sequence */
   char *tbpc_cnv; /* Out: sequence when read(2)ing it */
   u32 tbpc_seq_len;
   u32 tbpc_cnv_len;
   u32 tbpc_cnv_align_mask; /* For creating a_tty_bind_ctx.tbc_cnv */
   u32 tbpc_flags; /* mx_go_input_flags | a_tty_bind_flags */
};

/* Input character tree */
struct a_tty_bind_tree{
   struct a_tty_bind_tree *tbt_sibling; /* s at same level */
   struct a_tty_bind_tree *tbt_children; /* Sequence continues.. here */
   struct a_tty_bind_tree *tbt_parent;
   struct a_tty_bind_ctx *tbt_bind; /* NIL for intermediates */
   wchar_t tbt_char; /* acter this level represents */
   boole tbt_ismbseq; /* Is a follow-up character of a "multibyte" sequence */
   u8 tbt__dummy[3];
};
# endif /* mx_HAVE_KEY_BINDINGS */

struct a_tty_cell{
   wchar_t tc_wc;
   u16 tc_count; /* ..of bytes */
   u8 tc_width; /* Visual width; TAB==U8_MAX! */
   boole tc_novis; /* Do not display visually as such (control character) */
   char tc_cbuf[MB_LEN_MAX * 2]; /* .. plus reset shift sequence */
};

struct a_tty_global{
   struct a_tty_line *tg_line; /* To be able to access it from signal hdl */
# ifdef mx_HAVE_HISTORY
   struct a_tty_hist *tg_hist;
   struct a_tty_hist *tg_hist_tail;
   uz tg_hist_size;
   uz tg_hist_size_max;
# endif
# ifdef mx_HAVE_KEY_BINDINGS
   u32 tg_bind_cnt; /* Overall number of bindings */
   boole tg_bind_isdirty;
   boole tg_bind_isbuild;
#  define a_TTY_SHCUT_MAX (3 +1) /* Note: update manual on change! */
   u8 tg_bind__dummy[2];
   char tg_bind_shcut_cancel[mx__GO_INPUT_CTX_MAX1][a_TTY_SHCUT_MAX];
   char tg_bind_shcut_prompt_char[mx__GO_INPUT_CTX_MAX1][a_TTY_SHCUT_MAX];
   struct a_tty_bind_ctx *tg_bind[mx__GO_INPUT_CTX_MAX1];
   struct a_tty_bind_tree *tg_bind_tree[mx__GO_INPUT_CTX_MAX1][a_TTY_PRIME];
# endif
};
# ifdef mx_HAVE_KEY_BINDINGS
CTA(mx__GO_INPUT_CTX_MAX1 == 3 && a_TTY_SHCUT_MAX == 4 &&
   su_FIELD_SIZEOF(struct a_tty_global,tg_bind__dummy) == 2,
   "Value results in array sizes that results in bad structure layout");
CTA(a_TTY_SHCUT_MAX > 1,
   "Users need at least one shortcut, plus NUL terminator");
# endif

# ifdef mx_HAVE_HISTORY
struct a_tty_hist{
   struct a_tty_hist *th_older;
   struct a_tty_hist *th_younger;
   u32 th_len;
   u8 th_flags; /* enum a_tty_hist_flags */
   char th_dat[VFIELD_SIZE(3)];
};
CTA(U8_MAX >= a_TTY_HIST__MAX, "Value exceeds datatype storage");
# endif

#if defined mx_HAVE_KEY_BINDINGS || defined mx_HAVE_HISTORY
struct a_tty_input_ctx_map{
   BITENUM_IS(u32,mx_go_input_flags) ticm_ctx;
   char const ticm_name[12]; /* Name of `bind' context */
};
#endif

struct a_tty_line{
   /* Caller pointers */
   char **tl_x_buf;
   uz *tl_x_bufsize;
   /* Line data / content handling */
   u32 tl_count; /* ..of a_tty_cell's (<= a_TTY_LINE_MAX) */
   u32 tl_cursor; /* Current a_tty_cell insertion point */
   union{
      char *cbuf; /* *.tl_x_buf */
      struct a_tty_cell *cells;
   } tl_line;
   struct str tl_defc; /* Current default content */
   uz tl_defc_cursor_byte; /* Desired cursor position after takeover */
   struct str tl_savec; /* Saved default content */
   struct str tl_pastebuf; /* Last snarfed data */
# ifdef mx_HAVE_HISTORY
   struct a_tty_hist *tl_hist; /* History cursor */
# endif
   u32 tl_count_max; /* ..before buffer needs to grow */
   BITENUM_IS(u16,a_tty_config_flags) tl_conf_flags;
   /* Visual data representation handling */
   BITENUM_IS(u16,a_tty_visual_flags) tl_vi_flags;
   u32 tl_lst_count; /* .tl_count after last sync */
   u32 tl_lst_cursor; /* .tl_cursor after last sync */
   /* TODO Add another indirection layer by adding a tl_phy_line of
    * TODO a_tty_cell objects, incorporate changes in visual layer,
    * TODO then check what _really_ has changed, sync those changes only */
   struct a_tty_cell const *tl_phy_start; /* First visible cell, left border */
   u32 tl_phy_cursor; /* Physical cursor position */
   su_64( u8 tl__pad2[4]; )
   BITENUM_IS(u32,mx_go_input_flags) tl_goinflags;
   u32 tl_prompt_width;
   struct n_string *tl_prompt; /* Preformatted prompt (including colours) */
   char const *tl_prompt_base; /* Original prompt as passed to readline */
   /* .tl_pos_buf is a hack */
# ifdef mx_HAVE_COLOUR
   char *tl_pos_buf; /* mle-position colour-on, [4], reset seq. */
   char *tl_pos; /* Address of the [4] */
# endif
   /* Input processing */
# ifdef mx_HAVE_KEY_BINDINGS
   wchar_t tl_bind_takeover; /* Leftover byte to consume next */
   u8 tl_bind_inter_byte_timeout; /* In 1/10th secs */
   u8 tl_bind_inter_key_timeout; /*  ^ */
   u8 tl__bind_pad4[2];
   char (*tl_bind_shcut_cancel)[a_TTY_SHCUT_MAX]; /* Special _CANCEL control */
   char (*tl_bind_shcut_prompt_char)[a_TTY_SHCUT_MAX]; /* ..for _PROMPT_CHAR */
   struct a_tty_bind_tree *(*tl_bind_tree_hmap)[a_TTY_PRIME]; /* Lookup tree */
   struct a_tty_bind_tree *tl_bind_tree;
# endif
};

# if defined mx_HAVE_KEY_BINDINGS || defined mx_HAVE_HISTORY
/* C99: use [INDEX]={} */
CTAV(mx_GO_INPUT_CTX_BASE == 0);
CTAV(mx_GO_INPUT_CTX_DEFAULT == 1);
CTAV(mx_GO_INPUT_CTX_COMPOSE == 2);
static struct a_tty_input_ctx_map const
      a_tty_input_ctx_maps[mx__GO_INPUT_CTX_MAX1] = {
   FIELD_INITI(mx_GO_INPUT_CTX_BASE){mx_GO_INPUT_CTX_BASE, "base"},
   FIELD_INITI(mx_GO_INPUT_CTX_DEFAULT){mx_GO_INPUT_CTX_DEFAULT, "default"},
   FIELD_INITI(mx_GO_INPUT_CTX_COMPOSE){mx_GO_INPUT_CTX_COMPOSE, "compose"}
};
#endif

# ifdef mx_HAVE_KEY_BINDINGS
/* Special functions which our MLE provides internally.
 * Update the manual upon change! */
static char const a_tty_bind_fun_names[][24] = {
#  undef a_X
#  define a_X(I,N) \
   FIELD_INITI(a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## I)) "mle-" N "\0",

   a_X(BELL, "bell")
   a_X(GO_BWD, "go-bwd") a_X(GO_FWD, "go-fwd")
   a_X(GO_WORD_BWD, "go-word-bwd") a_X(GO_WORD_FWD, "go-word-fwd")
   a_X(GO_SCREEN_BWD, "go-screen-bwd") a_X(GO_SCREEN_FWD, "go-screen-fwd")
   a_X(GO_HOME, "go-home") a_X(GO_END, "go-end")
   a_X(DEL_BWD, "del-bwd") a_X(DEL_FWD, "del-fwd")
   a_X(SNARF_WORD_BWD, "snarf-word-bwd") a_X(SNARF_WORD_FWD, "snarf-word-fwd")
   a_X(SNARF_END, "snarf-end") a_X(SNARF_LINE, "snarf-line")
   a_X(HIST_BWD, "hist-bwd") a_X(HIST_FWD, "hist-fwd")
   a_X(HIST_SRCH_BWD, "hist-srch-bwd") a_X(HIST_SRCH_FWD, "hist-srch-fwd")
   a_X(REPAINT, "repaint")
   a_X(QUOTE_RNDTRIP, "quote-rndtrip")
   a_X(PROMPT_CHAR, "prompt-char")
   a_X(COMPLETE, "complete")
   a_X(PASTE, "paste")
   a_X(CLEAR_SCREEN, "clear-screen")

   a_X(RAISE_INT, "raise-int")
   a_X(RAISE_QUIT, "raise-quit")
   a_X(RAISE_TSTP, "raise-tstp")

   a_X(CANCEL, "cancel")
   a_X(RESET, "reset")
   a_X(FULLRESET, "fullreset")
   a_X(COMMIT, "commit")

#  undef a_X
};
# endif /* mx_HAVE_KEY_BINDINGS */

/* The default key bindings (unless disallowed).  Update manual upon change!
 * A logical subset of this table is also used if !mx_HAVE_KEY_BINDINGS (more
 * expensive than a switch() on control codes directly, but less redundant).
 * The table for the "base" context */
static struct a_tty_bind_builtin_tuple const a_tty_bind_base_tuples[] = {
# undef a_X
# define a_X(K,S) \
   {TRU1, K, 0, {'\0', (char)a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## S),}},

   a_X('A', GO_HOME)
   a_X('B', GO_BWD)
   a_X('C', RAISE_INT)
   a_X('D', DEL_FWD)
   a_X('E', GO_END)
   a_X('F', GO_FWD)
   a_X('G', RESET)
   a_X('H', DEL_BWD)
   a_X('I', COMPLETE)
   a_X('J', COMMIT)
   a_X('K', SNARF_END)
   a_X('L', REPAINT)
   /* M: same as J */
   a_X('N', HIST_FWD)
   /* O: below */
   a_X('P', HIST_BWD)
   a_X('Q', QUOTE_RNDTRIP)
   a_X('R', HIST_SRCH_BWD)
   a_X('S', HIST_SRCH_FWD)
   a_X('T', PASTE)
   a_X('U', SNARF_LINE)
   a_X('V', PROMPT_CHAR)
   a_X('W', SNARF_WORD_BWD)
   a_X('X', GO_WORD_FWD)
   a_X('Y', GO_WORD_BWD)
   a_X('Z', RAISE_TSTP)

   a_X('[', CANCEL)
   /* \: below */
   /* ]: below */
   /* ^: below */
   a_X('_', SNARF_WORD_FWD)

   a_X('?', DEL_BWD)

# undef a_X
# define a_X(K,S) {TRU1, K, 0, {S}},

   /* The remains only if we have `bind' functionality available */
# ifdef mx_HAVE_KEY_BINDINGS
#  undef a_X
#  define a_X(Q,S) \
   {FAL0, '\0', mx_TERMCAP_QUERY_ ## Q,\
      {'\0', (char)a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## S),}},

   a_X(key_backspace, DEL_BWD) a_X(key_dc, DEL_FWD)
   a_X(key_eol, SNARF_END)
   a_X(key_home, GO_HOME) a_X(key_end, GO_END)
   a_X(key_left, GO_BWD) a_X(key_right, GO_FWD)
   a_X(xkey_aleft, GO_WORD_BWD) a_X(xkey_aright, GO_WORD_FWD)
   a_X(xkey_cleft, GO_SCREEN_BWD) a_X(xkey_cright, GO_SCREEN_FWD)
   a_X(key_sleft, GO_HOME) a_X(key_sright, GO_END)
   a_X(key_up, HIST_BWD) a_X(key_down, HIST_FWD)
# endif /* mx_HAVE_KEY_BINDINGS */
};

/* The table for the "default" context */
static struct a_tty_bind_builtin_tuple const a_tty_bind_default_tuples[] = {
# undef a_X
# define a_X(K,S) \
   {TRU1, K, 0, {'\0', (char)a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## S),}},

# undef a_X
# define a_X(K,S) {TRU1, K, 0, {S}},

   a_X('O', "dt")

   a_X('\\', "z+")
   a_X(']', "z$")
   a_X('^', "z0")

   /* The remains only if we have `bind' functionality available */
# ifdef mx_HAVE_KEY_BINDINGS
#  undef a_X
#  define a_X(Q,S) {FAL0, '\0', mx_TERMCAP_QUERY_ ## Q, {S}},

   a_X(key_shome, "z0") a_X(key_send, "z$")
   a_X(xkey_sup, "z0") a_X(xkey_sdown, "z$")
   a_X(key_ppage, "z-") a_X(key_npage, "z+")
   a_X(xkey_cup, "dotmove-") a_X(xkey_cdown, "dotmove+")
# endif /* mx_HAVE_KEY_BINDINGS */
};
# undef a_X

static struct a_tty_global a_tty;

/* */
static boole a_tty_on_state_change(up cookie, u32 tiossc, s32 sig);

# ifdef mx_HAVE_HISTORY
/* Load and save the history file, respectively */
static boole a_tty_hist_load(void);
static boole a_tty_hist_save(void);

/* Initialize .tg_hist_size_max and return desired history file, or NIL */
static char const *a_tty_hist__query_config(void);

/* (More) Actions of `history'.
 * Return TRUM1 if synopsis shall be shown */
static boole a_tty_hist_clear(void);
static boole a_tty_hist_list(void);
static boole a_tty_hist_sel_or_del(char const **vec, boole dele);

/* Check whether a gabby history entry fits *history_gabby* */
static boole a_tty_hist_is_gabby_ok(BITENUM_IS(u32,mx_go_input_flags) gif);

/* (Definitely) Add an entry TODO yet assumes sigs_all_hold() is held!
 * Returns false on allocation failure */
static boole a_tty_hist_add(char const *s,
      BITENUM_IS(u32,mx_go_input_flags) gif);
# endif /* mx_HAVE_HISTORY*/

/* Setup configurable aspects of a line (the first time) */
static void a_tty_line_config(struct a_tty_line *tlp, boole first);

/* Adjust an active raw mode to use / not use a timeout */
# ifdef mx_HAVE_KEY_BINDINGS
static void a_tty_term_rawmode_timeout(struct a_tty_line *tlp,
      enum a_tty_term_timeout_mode ttm);
# endif

/* 0-X (2), U8_MAX == \t / HT */
static u8 a_tty_wcwidth(wchar_t wc);

/* Memory / cell / word generics */
static void a_tty_check_grow(struct a_tty_line *tlp, u32 no
      su_DBG_LOC_ARGS_DECL);
static sz a_tty_cell2dat(struct a_tty_line *tlp);
static void a_tty_cell2save(struct a_tty_line *tlp);

/* Save away data bytes of given range (max = non-inclusive) */
static void a_tty_copy2paste(struct a_tty_line *tlp, struct a_tty_cell *tcpmin,
      struct a_tty_cell *tcpmax);

/* Ask user for hexadecimal number, interpret as UTF-32 */
static wchar_t a_tty_vinuni(struct a_tty_line *tlp);

/* Visual screen synchronization */
static boole a_tty_vi_refresh(struct a_tty_line *tlp);

static boole a_tty_vi__paint(struct a_tty_line *tlp);

/* Search for word boundary, starting at tl_cursor, in "dir"ection (<> 0).
 * Return <0 when moving is impossible (backward direction but in position 0,
 * forward direction but in outermost column), and relative distance to
 * tl_cursor otherwise */
static s32 a_tty_wboundary(struct a_tty_line *tlp, s32 dir);

/* Most function implementations */
static void a_tty_khome(struct a_tty_line *tlp, boole dobell);
static void a_tty_kend(struct a_tty_line *tlp);
static void a_tty_kbs(struct a_tty_line *tlp);
static void a_tty_ksnarf(struct a_tty_line *tlp, boole cplline, boole dobell);
static s32 a_tty_kdel(struct a_tty_line *tlp);
static void a_tty_kleft(struct a_tty_line *tlp);
static void a_tty_kright(struct a_tty_line *tlp);
static void a_tty_ksnarfw(struct a_tty_line *tlp, boole fwd);
static void a_tty_kgow(struct a_tty_line *tlp, s32 dir);
static void a_tty_kgoscr(struct a_tty_line *tlp, s32 dir);
static boole a_tty_kother(struct a_tty_line *tlp, wchar_t wc);
static u32 a_tty_kht(struct a_tty_line *tlp);

# ifdef mx_HAVE_HISTORY
/* Return U32_MAX on "exhaustion" */
static u32 a_tty_khist(struct a_tty_line *tlp, boole fwd);
static u32 a_tty_khist_search(struct a_tty_line *tlp, boole fwd);

static u32 a_tty__khist_shared(struct a_tty_line *tlp, struct a_tty_hist *thp);
# endif

/* Handle a function */
static enum a_tty_fun_status a_tty_fun(struct a_tty_line *tlp,
      enum a_tty_bind_flags tbf, uz *len);

/* Readline core */
static sz a_tty_readline(struct a_tty_line *tlp, uz len, boole *histok_or_nil
      su_DBG_LOC_ARGS_DECL);

# ifdef mx_HAVE_KEY_BINDINGS
/* Find context or -1 */
static BITENUM_IS(u32,mx_go_input_flags) a_tty_bind_ctx_find(char const *name);

/* Create (or replace, if allowed) a binding */
static boole a_tty_bind_create(struct a_tty_bind_parse_ctx *tbpcp,
      boole replace);

/* Shared implementation to parse `bind' and `unbind' "key-sequence" and
 * "expansion" command line arguments into something that we can work with.
 * If isbindcmd==TRUM1 we allow empty sequences, if and only if a binding yet
 * exists -- supposed to be used for a subsequent _bind_show(), then */
static boole a_tty_bind_parse(struct a_tty_bind_parse_ctx *tbpcp,
      boole isbindcmd);

/* Lazy resolve a termcap(5)/terminfo(5) (or *termcap*!) capability */
static void a_tty_bind_resolve(struct a_tty_bind_ctx *tbcp);

/* Delete an existing binding */
static void a_tty_bind_del(struct a_tty_bind_parse_ctx *tbpcp);

/* Return number of lines printed */
static u32 a_tty_bind_show(struct a_tty_bind_ctx *tbcp, FILE *fp);

/* Life cycle of all input node trees */
static void a_tty_bind_tree_build(void);
static void a_tty_bind_tree_teardown(void);

static void a_tty__bind_tree_add(u32 hmap_idx,
      struct a_tty_bind_tree *store[a_TTY_PRIME], struct a_tty_bind_ctx *tbcp);
static struct a_tty_bind_tree *a_tty__bind_tree_add_wc(
      struct a_tty_bind_tree **treep, struct a_tty_bind_tree *parentp,
      wchar_t wc, boole ismbseq);
static void a_tty__bind_tree_dump(struct a_tty_bind_tree const *tbtp,
      char const *indent);
static void a_tty__bind_tree_free(struct a_tty_bind_tree *tbtp);
# endif /* mx_HAVE_KEY_BINDINGS */

static boole
a_tty_on_state_change(up cookie, u32 tiossc, s32 sig){
   boole rv;
   NYD; /* Signal handler */
   UNUSED(cookie);
   UNUSED(sig);

   rv = FAL0;

   if(tiossc & mx_TERMIOS_STATE_SUSPEND){
      mx_COLOUR( mx_colour_env_gut(); ) /* TODO NO SIMPLE SUSP POSSIBLE.. */
      mx_TERMCAP_SUSPEND((tiossc & mx_TERMIOS_STATE_POP) == 0 &&
         !(tiossc & mx_TERMIOS_STATE_SIGNAL));
      if((tiossc & (mx_TERMIOS_STATE_SIGNAL | mx_TERMIOS_STATE_JOB_SIGNAL)
            ) == mx_TERMIOS_STATE_SIGNAL)
         rv = TRU1;
   }else if(tiossc & mx_TERMIOS_STATE_RESUME){
      /* TODO THEREFORE NEED TO _GUT() .. _CREATE() ENTIRE ENVS!! */
      mx_COLOUR( mx_colour_env_create(mx_COLOUR_CTX_MLE, mx_tty_fp, FAL0); )
      mx_TERMCAP_RESUME(TRU1);
      a_tty.tg_line->tl_vi_flags |= a_TTY_VF_MOD_DIRTY;
      /* TODO Due to SA_RESTART we need to refresh the line in here! */
      a_tty_vi_refresh(a_tty.tg_line);
   }

   return rv;
}

# ifdef mx_HAVE_HISTORY
static boole
a_tty_hist_load(void){
   u8 version;
   uz lsize, cnt, llen;
   char *lbuf, *cp;
   FILE *fp;
   char const *hfname;
   boole rv;
   NYD_IN;

   rv = TRU1;

   if((hfname = a_tty_hist__query_config()) == NIL ||
         a_tty.tg_hist_size_max == 0)
      goto jleave;

   mx_sigs_all_holdx(); /* TODO too heavy, yet we may jump even here!? */

   if((fp = fopen(hfname, "r")) == NIL ||
         !mx_file_lock(fileno(fp), (mx_FILE_LOCK_MODE_TSHARE |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
      s32 eno;

      eno = su_err_no();
      n_err(_("Cannot read/lock *history-file*=%s: %s\n"),
         n_shexp_quote_cp(hfname, FAL0), su_err_doc(eno));
      rv = FAL0;
      goto jrele;
   }

   /* Clear old history */
   /* C99 */{
      struct a_tty_hist *thp;

      while((thp = a_tty.tg_hist) != NIL){
         a_tty.tg_hist = thp->th_older;
         su_FREE(thp);
      }
      a_tty.tg_hist_tail = NIL;
      a_tty.tg_hist_size = 0;
   }

   mx_fs_linepool_aquire(&lbuf, &lsize);

   cnt = S(uz,fsize(fp));
   version = 0;

   while(fgetline(&lbuf, &lsize, &cnt, &llen, fp, FAL0) != NIL){
      cp = lbuf;
      /* Hand-edited history files may have this, probably */
      while(llen > 0 && su_cs_is_space(cp[0])){
         ++cp;
         --llen;
      }
      if(llen > 0 && cp[llen - 1] == '\n')
         cp[--llen] = '\0';

      /* Skip empty and comment lines */
      if(llen == 0 || cp[0] == '#')
         continue;

      if(UNLIKELY(version == 0) &&
            (version = su_cs_cmp(cp, a_TTY_HIST_MARKER) ? 1 : 2) != 1)
         continue;

      /* C99 */{
         BITENUM_IS(u32,mx_go_input_flags) gif;

         if(version == 2){
            if(llen <= 2){
               /* XXX n_err(_("Skipped invalid *history-file* entry: %s\n"),
                * XXX  n_shexp_quote_cp(cp));*/
               continue;
            }
            switch(*cp++){
            default:
            case 'd':
               gif = mx_GO_INPUT_CTX_DEFAULT; /* == a_TTY_HIST_CTX_DEFAULT */
               break;
            case 'c':
               gif = mx_GO_INPUT_CTX_COMPOSE; /* == a_TTY_HIST_CTX_COMPOSE */
               break;
            }

            if(*cp++ == '*')
               gif |= mx_GO_INPUT_HIST_GABBY;

            while(*cp == ' ')
               ++cp;
         }else{
            gif = mx_GO_INPUT_CTX_DEFAULT;
            if(cp[0] == '*'){
               ++cp;
               gif |= mx_GO_INPUT_HIST_GABBY;
            }
         }

         if(!a_tty_hist_add(cp, gif))
            break;
      }
   }

   mx_fs_linepool_release(lbuf, lsize);

   if(ferror(fp))
      n_err(_("I/O error while reading *history-file*=%s\n"),
         n_shexp_quote_cp(hfname, FAL0));

jrele:
   if(fp != NIL)
      fclose(fp);

   mx_sigs_all_rele(); /* XXX remove jumps */
jleave:
   NYD_OU;
   return rv;
}

static boole
a_tty_hist_save(void){
   uz i;
   struct a_tty_hist *thp;
   FILE *fp;
   char const *v;
   boole rv, dogabby;
   NYD_IN;

   rv = TRU1;

   if((v = a_tty_hist__query_config()) == NIL || a_tty.tg_hist_size_max == 0)
      goto jleave;

   dogabby = ok_blook(history_gabby_persist);

   if((thp = a_tty.tg_hist) != NIL)
      for(i = a_tty.tg_hist_size_max; thp->th_older != NIL;
            thp = thp->th_older)
         if((dogabby || !(thp->th_flags & a_TTY_HIST_GABBY)) && --i == 0)
            break;

   mx_sigs_all_holdx(); /* TODO too heavy, yet we may jump even here!? */

   /* TODO temporary histfile + rename?! */
   if((fp = fopen(v, "w")) == NIL ||
         !mx_file_lock(fileno(fp), (mx_FILE_LOCK_MODE_TEXCL |
            mx_FILE_LOCK_MODE_RETRY | mx_FILE_LOCK_MODE_LOG))){
      int e;

      e = su_err_no();
      n_err(_("Cannot write/lock *history-file*=%s: %s\n"),
         n_shexp_quote_cp(v, FAL0), su_err_doc(e));
      rv = FAL0;
      goto jrele;
   }

   if(fwrite(a_TTY_HIST_MARKER "\n", sizeof *a_TTY_HIST_MARKER,
            sizeof(a_TTY_HIST_MARKER "\n") -1, fp) !=
         sizeof(a_TTY_HIST_MARKER "\n") -1)
      goto jioerr;
   else for(; thp != NIL; thp = thp->th_younger){
      if(dogabby || !(thp->th_flags & a_TTY_HIST_GABBY)){
         char c;

         switch(thp->th_flags & a_TTY_HIST_CTX_MASK){
         default:
         case a_TTY_HIST_CTX_DEFAULT:
            c = 'd';
            break;
         case a_TTY_HIST_CTX_COMPOSE:
            c = 'c';
            break;
         }
         if(putc(c, fp) == EOF)
            goto jioerr;

         if((thp->th_flags & a_TTY_HIST_GABBY) && putc('*', fp) == EOF)
            goto jioerr;

         if(putc(' ', fp) == EOF ||
               fwrite(thp->th_dat, sizeof *thp->th_dat, thp->th_len, fp) !=
                  sizeof(*thp->th_dat) * thp->th_len ||
               putc('\n', fp) == EOF){
jioerr:
            n_err(_("I/O error while writing *history-file* %s\n"),
               n_shexp_quote_cp(v, FAL0));
            rv = FAL0;
            break;
         }
      }
   }

jrele:
   if(fp != NIL)
      fclose(fp);

   mx_sigs_all_rele(); /* XXX remove jumps */
jleave:
   NYD_OU;
   return rv;
}

static char const *
a_tty_hist__query_config(void){
   char const *rv, *cp;
   NYD2_IN;

   if((cp = ok_vlook(NAIL_HISTSIZE)) != NIL)
      n_OBSOLETE(_("please use *history-size* instead of *NAIL_HISTSIZE*"));
   if((rv = ok_vlook(history_size)) == NIL)
      rv = cp;
   if(rv == NIL)
      a_tty.tg_hist_size_max = UZ_MAX;
   else
      (void)su_idec_uz_cp(&a_tty.tg_hist_size_max, rv, 10, NIL);

   if((cp = ok_vlook(NAIL_HISTFILE)) != NIL)
      n_OBSOLETE(_("please use *history-file* instead of *NAIL_HISTFILE*"));
   if((rv = ok_vlook(history_file)) == NIL)
      rv = cp;
   if(rv != NIL)
      rv = fexpand(rv, (FEXP_NOPROTO | FEXP_LOCAL_FILE | FEXP_NSHELL));

   NYD2_OU;
   return rv;
}

static boole
a_tty_hist_clear(void){
   struct a_tty_hist *thp;
   NYD_IN;

   while((thp = a_tty.tg_hist) != NIL){
      a_tty.tg_hist = thp->th_older;
      su_FREE(thp);
   }
   a_tty.tg_hist_tail = NIL;
   a_tty.tg_hist_size = 0;

   NYD_OU;
   return TRU1;
}

static boole
a_tty_hist_list(void){
   struct a_tty_hist *thp;
   uz no, l, b;
   FILE *fp;
   NYD_IN;

   if(a_tty.tg_hist == NIL)
      goto jleave;

   if((fp = mx_fs_tmp_open(NIL, "hist", (mx_FS_O_RDWR | mx_FS_O_UNLINK), NIL)
            ) == NIL)
      fp = n_stdout;

   no = a_tty.tg_hist_size;
   l = b = 0;

   for(thp = a_tty.tg_hist; thp != NIL; --no, ++l, thp = thp->th_older){
      char c1, c2;

      b += thp->th_len;

      switch(thp->th_flags & a_TTY_HIST_CTX_MASK){
      default:
      case a_TTY_HIST_CTX_DEFAULT:
         c1 = 'd';
         break;
      case a_TTY_HIST_CTX_COMPOSE:
         c1 = 'c';
         break;
      }
      c2 = (thp->th_flags & a_TTY_HIST_GABBY) ? '*' : ' ';

      if(n_poption & n_PO_D_V)
         fprintf(fp, "# Length +%" PRIu32 ", total %" PRIuZ "\n",
            thp->th_len, b);
      fprintf(fp, "%c%c%4" PRIuZ "\t%s\n", c1, c2, no, thp->th_dat);
   }

   if(fp != n_stdout){
      page_or_print(fp, l);

      mx_fs_close(fp);
   }else
      clearerr(fp);

jleave:
   NYD_OU;
   return TRU1;
}

static boole
a_tty_hist_sel_or_del(char const **vec, boole dele){
   struct a_tty_hist *thp, *othp, *ythp;
   boole rv;
   sz *lp_base, *lp, entry, delcnt, ep;
   NYD_IN;
   LCTAV(sizeof(sz) == sizeof(void*));

   for(entry = 0; vec[entry] != NIL; ++entry)
      ;

   if(entry == 0 || (!dele && entry > 1)){
      rv = TRUM1;
      goto j_leave;
   }

   lp_base = lp = su_LOFI_TALLOC(sz, ++entry);
   rv = FAL0;

   for(; *vec != NIL; ++vec){
      if((su_idec_sz_cp(lp, *vec, 10, NIL
               ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
            ) == su_IDEC_STATE_CONSUMED)
         ++lp;
      else
         n_err(_("history: not a number, or no such entry: %s\n"), *vec);
   }
   *lp = 0;

   if(lp == lp_base){
      if(!dele)
         rv = TRUM1;
      goto jleave;
   }

   su_sort_shell_vpp(C(void const**,&lp_base[0]), entry -1, NIL);

   rv = TRU1;

   for(delcnt = 0, lp = lp_base; *lp != 0; ++lp){
      if(delcnt != 0 && entry == *lp)
         continue;
      entry = *lp;
      ep = (entry < 0) ? -entry : entry;
      ep -= delcnt++;

      if(ep == 0 || UCMP(z, ep, >, a_tty.tg_hist_size)){
         n_err(_("history: not a number, or no such entry: %" PRIdZ "\n"),
            *lp);
         rv = FAL0;
         continue;
      }

      if(entry < 0)
         --ep;
      else
         ep = S(sz,a_tty.tg_hist_size) - ep;

      for(thp = a_tty.tg_hist; ep-- != 0; thp = thp->th_older)
         ASSERT(thp != NIL);

      ASSERT(thp != NIL);
      othp = thp->th_older;
      ythp = thp->th_younger;
      if(othp != NIL)
         othp->th_younger = ythp;
      else
         a_tty.tg_hist_tail = ythp;
      if(ythp != NIL)
         ythp->th_older = othp;
      else
         a_tty.tg_hist = othp;

      if(!dele){ /* XXX c_history(): needless double relinking done */
         if((thp->th_older = a_tty.tg_hist) != NIL)
            a_tty.tg_hist->th_younger = thp;
         else
            a_tty.tg_hist_tail = thp;
         thp->th_younger = NIL;
          a_tty.tg_hist = thp;

         mx_go_input_inject(mx_GO_INPUT_INJECT_COMMIT,
            thp->th_dat, thp->th_len);
         break;
      }else{
         --a_tty.tg_hist_size;
         fprintf(mx_tty_fp, _("history: deleting %" PRIdZ ": %s\n"),
            entry, thp->th_dat);
         su_FREE(thp);
      }
   }

jleave:
   su_LOFI_FREE(lp_base);

j_leave:
   NYD_OU;
   return rv;
}

static boole
a_tty_hist_is_gabby_ok(BITENUM_IS(u32,mx_go_input_flags) gif){
   char const *cp;
   boole rv;
   NYD2_IN;

   if((cp = ok_vlook(history_gabby)) == NIL)
      rv = FAL0;
   else if(!(gif & mx_GO_INPUT_HIST_ERROR))
      rv = TRU1;
   else{
      static char const wlist[][8] = {"all", "errors\0"};
      uz i;
      char *buf, *e;

      buf = savestr(cp);
      while((e = su_cs_sep_c(&buf, ',', TRU1)) != NIL)
         for(i = 0;;)
            if(!su_cs_cmp_case(e, wlist[i])){
               rv = TRU1;
               goto jwl_ok;
            }else if(++i == NELEM(wlist)){
               n_err(_("*history-gabby*: unknown keyword: %s\n"),
                  n_shexp_quote_cp(e, FAL0));
               break;
            }
      rv = FAL0;
jwl_ok:;
   }

   NYD2_OU;
   return rv;
}

static boole
a_tty_hist_add(char const *s, BITENUM_IS(u32,mx_go_input_flags) gif){
   struct a_tty_hist *thp, *othp, *ythp;
   u32 l;
   NYD2_IN;

   l = S(u32,su_cs_len(s)); /* xxx simply do not store if >= S32_MAX */

   /* Eliminating duplicates is expensive, but simply inacceptable so
    * during the load of a potentially large history file! */
   if(n_psonce & n_PSO_LINE_EDITOR_INIT)
      for(thp = a_tty.tg_hist; thp != NIL; thp = thp->th_older)
         if(thp->th_len == l && !su_cs_cmp(thp->th_dat, s)){
            /* xxx Do not propagate an existing non-gabby entry to gabby */
            thp->th_flags = (gif & a_TTY_HIST_CTX_MASK) |
                  (((gif & mx_GO_INPUT_HIST_GABBY) &&
                     (thp->th_flags & a_TTY_HIST_GABBY)
                   ) ? a_TTY_HIST_GABBY : 0);
            othp = thp->th_older;
            ythp = thp->th_younger;
            if(othp != NIL)
               othp->th_younger = ythp;
            else
               a_tty.tg_hist_tail = ythp;
            if(ythp != NIL)
               ythp->th_older = othp;
            else
               a_tty.tg_hist = othp;
            goto jleave;
         }

   /* If ring is full, rotate*/
   if(LIKELY(a_tty.tg_hist_size <= a_tty.tg_hist_size_max))
      ++a_tty.tg_hist_size;
   else{
      --a_tty.tg_hist_size;
      if((thp = a_tty.tg_hist_tail) != NIL){
         if((a_tty.tg_hist_tail = thp->th_younger) == NIL)
            a_tty.tg_hist = NIL;
         else
            a_tty.tg_hist_tail->th_older = NIL;
         su_FREE(thp);
      }
   }

   thp = su_MEM_ALLOCATE(VSTRUCT_SIZEOF(struct a_tty_hist,th_dat) + l +1,
         1, su_MEM_ALLOC_NOMEM_OK);
   if(thp == NIL)
      goto j_leave;
   thp->th_len = l;
   thp->th_flags = (gif & a_TTY_HIST_CTX_MASK) |
         (gif & mx_GO_INPUT_HIST_GABBY ? a_TTY_HIST_GABBY : 0);
   su_mem_copy(thp->th_dat, s, l +1);

jleave:
   if((thp->th_older = a_tty.tg_hist) != NIL)
      a_tty.tg_hist->th_younger = thp;
   else
      a_tty.tg_hist_tail = thp;
   thp->th_younger = NIL;
   a_tty.tg_hist = thp;

j_leave:
   NYD2_OU;
   return (thp != NIL);
}
# endif /* mx_HAVE_HISTORY */

static void
a_tty_line_config(struct a_tty_line *tlp, boole first){
   NYD2_IN;

   /* *line-editor-config* */
   /* C99 */{
      static struct{
         char const name[15];
         u8 flag;
      } const ca[] = {
         {"quote-rndtrip", a_TTY_CONF_QUOTE_RNDTRIP},
         {"srch-case", a_TTY_CONF_SRCH_CASE},
         {"srch-pos0", a_TTY_CONF_SRCH_POS0},
         {"srch-any", a_TTY_CONF_SRCH_ANY}
#ifdef mx_HAVE_REGEX
         ,{"srch-regex", a_TTY_CONF_SRCH_REGEX}
#endif
      };
      uz i;
      char *buf, *e;

      tlp->tl_conf_flags &= ~a_TTY__CONF_MASK;

      if((buf = UNCONST(char*,ok_vlook(line_editor_config))) != NIL){
         for(buf = savestr(buf); (e = su_cs_sep_c(&buf, ',', TRU1)) != NIL;){
            for(i = 0;;){
               if(!su_cs_cmp_case(e, ca[i].name)){
                  tlp->tl_conf_flags |= ca[i].flag;
                  break;
               }else if(++i == NELEM(ca)){
                  n_err(_("*line-editor-config*: unsupported keyword: %s\n"),
                     n_shexp_quote_cp(e, FAL0));
                  break;
               }
            }
         }

#ifdef mx_HAVE_REGEX
         if((tlp->tl_conf_flags & a_TTY_CONF_SRCH_ANY) &&
               (tlp->tl_conf_flags & a_TTY_CONF_SRCH_REGEX))
            n_err(_("*line-editor-config*: srch-regex and srch-any are mutual "
                  "exclusive\n"));
#endif
      }
   }

# ifdef mx_HAVE_COLOUR
   /* C99 */{
      char *posbuf, *pos;

      if(first)
         mx_colour_env_create(mx_COLOUR_CTX_MLE, mx_tty_fp, FAL0);

      /* .tl_pos_buf is a hack */
      posbuf = pos = NIL;

      if(mx_COLOUR_IS_ACTIVE()){
         char const *ccol;
         struct mx_colour_pen *ccp;
         struct str const *s;

         if((ccp = mx_colour_pen_create(mx_COLOUR_ID_MLE_POSITION, NIL)
               ) != NIL && (s = mx_colour_pen_to_str(ccp)) != NIL){
            ccol = s->s;
            if((s = mx_colour_reset_to_str()) != NIL){
               uz l1, l2;

               l1 = su_cs_len(ccol);
               l2 = su_cs_len(s->s);
               posbuf = su_AUTO_ALLOC(l1 + 4 + l2 +1);
               su_mem_copy(posbuf, ccol, l1);
               pos = &posbuf[l1];
               su_mem_copy(&pos[4], s->s, ++l2);
            }
         }
      }

      if(posbuf == NIL){
         posbuf = pos = su_AUTO_ALLOC(4 +1);
         pos[4] = '\0';
      }

      tlp->tl_pos_buf = posbuf;
      tlp->tl_pos = pos;
   }
# endif /* mx_HAVE_COLOUR */

   /* Prompt after colour */
   if(!(tlp->tl_goinflags & mx_GO_INPUT_PROMPT_NONE))
      tlp->tl_prompt_width = mx_tty_create_prompt(tlp->tl_prompt,
            tlp->tl_prompt_base, tlp->tl_goinflags);

   /* The bind tree last, it is most expensive and has not really something
    * to do with the line as such */
# ifdef mx_HAVE_KEY_BINDINGS
   /* C99 */{
      char const *name, *cp;
      u8 *destp;

      destp = &tlp->tl_bind_inter_byte_timeout;
      name = "byte";
      cp = ok_vlook(bind_inter_byte_timeout);

jbind_timeout_redo:
      if(cp != NIL){
         uz const uit_max = 100u * U8_MAX;
         u32 is;
         uz uit;

         /* TODO generic variable `set' should have capability to ensure
          * TODO integer limits upon assignment */
         if(((is = su_idec_uz_cp(&uit, cp, 0, NIL)) & su_IDEC_STATE_EMASK) ||
               !(is & su_IDEC_STATE_CONSUMED) || uit > uit_max){
            if(n_poption & n_PO_D_V)
               n_err(_("*bind-inter-%s-timeout* invalid, using %" PRIuZ
                  ": %s\n"), name, uit_max, cp);
            uit = uit_max;
         }

         /* Convert to the tenths of seconds that we need to use */
         *destp = S(u8,(uit + 99) / 100);
      }

      if(destp == &tlp->tl_bind_inter_byte_timeout){
         destp = &tlp->tl_bind_inter_key_timeout;
         name = "key";
         cp = ok_vlook(bind_inter_key_timeout);
         goto jbind_timeout_redo;
      }

      if(a_tty.tg_bind_isdirty)
         a_tty_bind_tree_teardown();
      if(a_tty.tg_bind_cnt > 0 && !a_tty.tg_bind_isbuild)
         a_tty_bind_tree_build();
      tlp->tl_bind_tree_hmap =
            &a_tty.tg_bind_tree[tlp->tl_goinflags & mx__GO_INPUT_CTX_MASK];
      tlp->tl_bind_shcut_cancel =
            &a_tty.tg_bind_shcut_cancel[tlp->tl_goinflags &
               mx__GO_INPUT_CTX_MASK];
      tlp->tl_bind_shcut_prompt_char =
            &a_tty.tg_bind_shcut_prompt_char[tlp->tl_goinflags &
               mx__GO_INPUT_CTX_MASK];
   }
# endif /* mx_HAVE_KEY_BINDINGS */

   NYD2_OU;
}

# ifdef mx_HAVE_KEY_BINDINGS
static void
a_tty_term_rawmode_timeout(struct a_tty_line *tlp,
      enum a_tty_term_timeout_mode ttm){
   u32 tiosc;
   u8 i, j;
   NYD2_IN;

   if(ttm != a_TTY_TTM_NONE){
      tiosc = mx_TERMIOS_CMD_RAW_TIMEOUT;
      i = j = tlp->tl_bind_inter_byte_timeout;
      if(ttm == a_TTY_TTM_MBSEQ){
         /* If that is 0, whee, that is fast */
      }else if((i = tlp->tl_bind_inter_key_timeout) == 0)
         goto jntimeout;
      else if(ttm == a_TTY_TTM_KEY_AFTER_MBSEQ && i > j)
         i -= j;
   }else{
jntimeout:
      tiosc = mx_TERMIOS_CMD_RAW;
      i = 1;
   }

   mx_termios_cmd(tiosc, i);
   NYD2_OU;
}
# endif /* mx_HAVE_KEY_BINDINGS */

static u8
a_tty_wcwidth(wchar_t wc){
   u8 rv;
   NYD2_IN;

   /* Special case the reverse solidus at first */
   if(wc == '\t')
      rv = U8_MAX;
   else{
      int i;

# ifdef mx_HAVE_WCWIDTH
      rv = ((i = wcwidth(wc)) > 0) ? S(u8,i) : 0;
# else
      rv = iswprint(wc) ? 1 + (wc >= 0x1100u) : 0; /* TODO use S-CText */
# endif
   }
   NYD2_OU;
   return rv;
}

static void
a_tty_check_grow(struct a_tty_line *tlp, u32 no  su_DBG_LOC_ARGS_DECL){
   u32 cmax;
   NYD2_IN;

   if(UNLIKELY((cmax = tlp->tl_count + no) > tlp->tl_count_max)){
      uz i;

      i = cmax * sizeof(struct a_tty_cell) + 2 * sizeof(struct a_tty_cell);
      if(LIKELY(i >= *tlp->tl_x_bufsize)){
         mx_sigs_all_holdx(); /* XXX v15 drop */
         i <<= 1;
         tlp->tl_line.cbuf =
         *tlp->tl_x_buf = su_MEM_REALLOC_LOCOR(*tlp->tl_x_buf, i,
               su_DBG_LOC_ARGS_ORUSE);
         *tlp->tl_x_bufsize = i;
         mx_sigs_all_rele(); /* XXX v15 drop */
      }
      tlp->tl_count_max = cmax;
   }

   NYD2_OU;
}

static sz
a_tty_cell2dat(struct a_tty_line *tlp){
   uz len, i;
   NYD2_IN;

   len = 0;

   if(LIKELY((i = tlp->tl_count) > 0)){
      struct a_tty_cell const *tcap;

      tcap = tlp->tl_line.cells;
      do{
         su_mem_copy(tlp->tl_line.cbuf + len, tcap->tc_cbuf, tcap->tc_count);
         len += tcap->tc_count;
      }while(++tcap, --i > 0);
   }

   tlp->tl_line.cbuf[len] = '\0';
   NYD2_OU;
   return S(sz,len);
}

static void
a_tty_cell2save(struct a_tty_line *tlp){
   uz len, i;
   struct a_tty_cell *tcap;
   NYD2_IN;

   tlp->tl_savec.s = NIL;
   tlp->tl_savec.l = 0;

   if(UNLIKELY(tlp->tl_count == 0))
      goto jleave;

   for(tcap = tlp->tl_line.cells, len = 0, i = tlp->tl_count; i > 0;
         ++tcap, --i)
      len += tcap->tc_count;

   tlp->tl_savec.s = su_AUTO_ALLOC((tlp->tl_savec.l = len) +1);

   for(tcap = tlp->tl_line.cells, len = 0, i = tlp->tl_count; i > 0;
         ++tcap, --i){
      su_mem_copy(tlp->tl_savec.s + len, tcap->tc_cbuf, tcap->tc_count);
      len += tcap->tc_count;
   }
   tlp->tl_savec.s[len] = '\0';

jleave:
   NYD2_OU;
}

static void
a_tty_copy2paste(struct a_tty_line *tlp, struct a_tty_cell *tcpmin,
      struct a_tty_cell *tcpmax){
   char *cp;
   struct a_tty_cell *tcp;
   uz l;
   NYD2_IN;

   l = 0;
   for(tcp = tcpmin; tcp < tcpmax; ++tcp)
      l += tcp->tc_count;

   tlp->tl_pastebuf.s = cp = su_AUTO_ALLOC((tlp->tl_pastebuf.l = l) +1);

   for(tcp = tcpmin; tcp < tcpmax; cp += l, ++tcp)
      su_mem_copy(cp, tcp->tc_cbuf, l = tcp->tc_count);
   *cp = '\0';

   NYD2_OU;
}

static wchar_t
a_tty_vinuni(struct a_tty_line *tlp){
   char buf[16];
   uz i;
   wchar_t wc;
   NYD2_IN;

   wc = '\0';

   if(!mx_termcap_cmdx(mx_TERMCAP_CMD_cr) ||
         !mx_termcap_cmd(mx_TERMCAP_CMD_ce, 0, -1))
      goto jleave;

   /* C99 */{
      struct str const *cpre, *csuf;

      cpre = csuf = NIL;
#ifdef mx_HAVE_COLOUR
      if(mx_COLOUR_IS_ACTIVE()){
         struct mx_colour_pen *cpen;

         cpen = mx_colour_pen_create(mx_COLOUR_ID_MLE_PROMPT, NIL);
         if((cpre = mx_colour_pen_to_str(cpen)) != NIL)
            csuf = mx_colour_reset_to_str();
      }
#endif
      fprintf(mx_tty_fp, _("%sPlease enter Unicode code point:%s "),
         (cpre != NIL ? cpre->s : su_empty),
         (csuf != NIL ? csuf->s : su_empty));
   }
   fflush(mx_tty_fp);

   buf[sizeof(buf) -1] = '\0';
   for(i = 0;;){
      if(read(STDIN_FILENO, &buf[i], 1) != 1){ /* xxx tty_fd */
         if(su_err_no() == su_ERR_INTR) /* xxx #if !SA_RESTART ? */
            continue;
         goto jleave;
      }
      if(buf[i] == '\n')
         break;
      if(!su_cs_is_xdigit(buf[i])){
         char const emsg[] = "[0-9a-fA-F]";

         LCTA(sizeof emsg <= sizeof(buf), "Preallocated buffer too small");
         su_mem_copy(buf, emsg, sizeof emsg);
         goto jerr;
      }

      putc(buf[i], mx_tty_fp);
      fflush(mx_tty_fp);
      if(++i == sizeof buf)
         goto jerr;
   }
   buf[i] = '\0';

   if((su_idec_uz_cp(&i, buf, 16, NIL
            ) & (su_IDEC_STATE_EMASK | su_IDEC_STATE_CONSUMED)
         ) != su_IDEC_STATE_CONSUMED || i > 0x10FFFF/* XXX magic; CText */){
jerr:
      n_err(_("\nInvalid input: %s\n"), buf);
      goto jleave;
   }

   wc = S(wchar_t,i); /* XXX Ctext (ISO C wchar_t not necessarily Unicode */
jleave:
   tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY | (wc == '\0' ? a_TTY_VF_BELL : 0);

   NYD2_OU;
   return wc;
}

static boole
a_tty_vi_refresh(struct a_tty_line *tlp){
   boole rv;
   NYD2_IN;

   if(tlp->tl_vi_flags & a_TTY_VF_BELL){ /* XXX visual bell?? */
      tlp->tl_vi_flags |= a_TTY_VF_SYNC;
      if(putc('\a', mx_tty_fp) == EOF)
         goto jerr;
   }

   if(tlp->tl_vi_flags & a_TTY_VF_REFRESH){
      /* kht may want to restore a cursor position after inserting some
       * data somewhere */
      if(tlp->tl_defc_cursor_byte > 0){
         uz i, j;
         sz k;

         a_tty_khome(tlp, FAL0);

         i = tlp->tl_defc_cursor_byte;
         tlp->tl_defc_cursor_byte = 0;
         for(j = 0; tlp->tl_cursor < tlp->tl_count; ++j){
            a_tty_kright(tlp);
            if((k = tlp->tl_line.cells[j].tc_count) > i)
               break;
            i -= k;
         }
      }

      if(!a_tty_vi__paint(tlp))
         goto jerr;
   }

   if(tlp->tl_vi_flags & a_TTY_VF_SYNC){
      tlp->tl_vi_flags &= ~a_TTY_VF_SYNC;
      if(fflush(mx_tty_fp))
         goto jerr;
   }

   rv = TRU1;
jleave:
   tlp->tl_vi_flags &= ~a_TTY_VF_ALL_MASK;
   NYD2_OU;
   return rv;

jerr:
   clearerr(mx_tty_fp); /* xxx I/O layer rewrite */
   n_err(_("Visual refresh failed!  Is $TERM set correctly?\n"
      "  Setting *line-editor-disable* to get us through!\n"));
   ok_bset(line_editor_disable);
   rv = FAL0;
   goto jleave;
}

static boole
a_tty_vi__paint(struct a_tty_line *tlp){
   enum{
      a_TRUE_RV = a_TTY__VF_LAST<<1, /* Return value bit */
      a_HAVE_PROMPT = a_TTY__VF_LAST<<2, /* Have a prompt */
      a_SHOW_PROMPT = a_TTY__VF_LAST<<3, /* Shall print the prompt */
      a_MOVE_CURSOR = a_TTY__VF_LAST<<4, /* Move visual cursor for user! */
      a_LEFT_MIN = a_TTY__VF_LAST<<5, /* Left boundary on screen */
      a_RIGHT_MAX = a_TTY__VF_LAST<<6, /* .. */
      a_HAVE_POSITION = a_TTY__VF_LAST<<7, /* Print the position indicator */
      a_RECALC_PHY_CURSOR = a_TTY__VF_LAST<<8, /* Update knowledge of.. */
      a__LAST = a_RECALC_PHY_CURSOR
   };

   u32 f, w, phy_wid_base, phy_wid, phy_base, phy_cur, cnt,
      ASSERT_INJ(lstcur su_COMMA) cur,
      /*vi_left,*/ /*vi_right,*/ phy_nxtcur;
   struct a_tty_cell const *tccp, *tcp_left, *tcp_right, *tcxp;
   NYD2_IN;
   LCTA(UCMP(64, a__LAST, <, U32_MAX), "Flag bits excess storage type");

   f = tlp->tl_vi_flags;
   tlp->tl_vi_flags = (f & ~a_TTY_VF_ALL_MASK) | a_TTY_VF_SYNC;
   f |= a_TRUE_RV;
   if((w = tlp->tl_prompt_width) > 0)
      f |= a_HAVE_PROMPT;
   f |= a_HAVE_POSITION;

   /* xxx We do not have a OnTerminalResize event (see termios) yet, so we need
    * xxx to reevaluate our circumstances over and over again */
   /* Do not display prompt or position indicator on very small screens */
   if((phy_wid_base = mx_termios_dimen.tiosd_width) <= a_TTY_WIDTH_RIPOFF)
      f &= ~(a_HAVE_PROMPT | a_HAVE_POSITION);
   else{
      phy_wid_base -= a_TTY_WIDTH_RIPOFF;

      /* Disable the prompt if the screen is too small; due to lack of some
       * indicator simply add a second ripoff */
      if((f & a_HAVE_PROMPT) && w + a_TTY_WIDTH_RIPOFF >= phy_wid_base)
         f &= ~a_HAVE_PROMPT;
   }

   phy_wid = phy_wid_base;
   phy_base = 0;
   phy_cur = tlp->tl_phy_cursor;
   cnt = tlp->tl_count;
   ASSERT_INJ( lstcur = tlp->tl_lst_cursor; )

   /* XXX Assume dirty screen if shrunk */
   if(cnt < tlp->tl_lst_count)
      f |= a_TTY_VF_MOD_DIRTY;

   /* TODO Without mx_HAVE_TERMCAP, it would likely be much cheaper to simply
    * TODO always "cr + paint + ce + ch", since ce is simulated via spaces.. */

   /* Quickshot: if the line is empty, possibly print prompt and out */
   if(cnt == 0){
      /* In that special case dirty anything if it seems better */
      if((f & a_TTY_VF_MOD_CONTENT) || tlp->tl_lst_count > 0)
         f |= a_TTY_VF_MOD_DIRTY;

      if((f & a_TTY_VF_MOD_DIRTY) && phy_cur != 0){
         if(!mx_termcap_cmdx(mx_TERMCAP_CMD_cr))
            goto jerr;
         phy_cur = 0;
      }

      if((f & (a_TTY_VF_MOD_DIRTY | a_HAVE_PROMPT)) ==
            (a_TTY_VF_MOD_DIRTY | a_HAVE_PROMPT)){
         if(fputs(n_string_cp(tlp->tl_prompt), mx_tty_fp) == EOF)
            goto jerr;
         phy_cur = tlp->tl_prompt_width + 1;
      }

      /* May need to clear former line content */
      if((f & a_TTY_VF_MOD_DIRTY) &&
            !mx_termcap_cmd(mx_TERMCAP_CMD_ce, phy_cur, -1))
         goto jerr;

      tlp->tl_phy_start = tlp->tl_line.cells;
      goto jleave;
   }

   /* Try to get an idea of the visual window */

   /* Find the left visual boundary */
   phy_wid = (phy_wid >> 1) + (phy_wid >> 2);
   if(f & a_TTY_VF_MAXWIDTH_POS0)
      tlp->tl_cursor = cur = 0;
   else if((cur = tlp->tl_cursor) == cnt)
      --cur;

   w = (tcp_left = tccp = tlp->tl_line.cells + cur)->tc_width;
   if(w == U8_MAX) /* TODO yet HT == SPACE */
      w = 1;
   while(tcp_left > tlp->tl_line.cells){
      u16 cw = tcp_left[-1].tc_width;

      if(cw == U8_MAX) /* TODO yet HT == SPACE */
         cw = 1;
      if(w + cw >= phy_wid)
         break;
      w += cw;
      --tcp_left;
   }
   /*vi_left = w;*/

   /* If the left hand side of our visual viewpoint consumes less than half
    * of the screen width, show the prompt */
   if(tcp_left == tlp->tl_line.cells)
      f |= a_LEFT_MIN;

   if((f & (a_LEFT_MIN | a_HAVE_PROMPT)) == (a_LEFT_MIN | a_HAVE_PROMPT) &&
         w + tlp->tl_prompt_width < phy_wid){
      phy_base = tlp->tl_prompt_width;
      f |= a_SHOW_PROMPT;
   }

   /* Then search for right boundary.  Dependent upon n_PSO_FULLWIDTH (termcap
    * am/xn) we leave the rightmost column empty because some terminals
    * [cw]ould wrap the line if we write into that, or not.
    * TODO We do not deal with !mx_TERMCAP_QUERY_sam */
   phy_wid = phy_wid_base - phy_base;
   tcp_right = tlp->tl_line.cells + cnt;

   while(&tccp[1] < tcp_right){
      u16 cw = tccp[1].tc_width;
      u32 i;

      if(cw == U8_MAX) /* TODO yet HT == SPACE */
         cw = 1;
      i = w + cw;
      if(i > phy_wid)
         break;
      w = i;
      ++tccp;
   }
   /*vi_right = w - vi_left;*/

   /* If the complete line including prompt fits on the screen, show prompt */
   if(--tcp_right == tccp){
      f |= a_RIGHT_MAX;

      /* Since we did brute-force walk also for the left boundary we may end up
       * in a situation were anything effectively fits on the screen, including
       * the prompt that is, but where we do not recognize this since we
       * restricted the search to fit in some visual viewpoint.  Therefore try
       * again to extend the left boundary to overcome that */
      if(!(f & a_LEFT_MIN)){
         struct a_tty_cell const *tc1p = tlp->tl_line.cells;
         /*u32 vil1 = vi_left;*/

         ASSERT(!(f & a_SHOW_PROMPT));
         w += tlp->tl_prompt_width;
         for(tcxp = tcp_left;;){
            u32 i = tcxp[-1].tc_width;

            if(i == U8_MAX) /* TODO yet HT == SPACE */
               i = 1;
            /*vil1 += i;*/
            i += w;
            if(i > phy_wid)
               break;
            w = i;
            if(--tcxp == tc1p){
               tcp_left = tc1p;
               /*vi_left = vil1;*/
               f |= a_LEFT_MIN;
               break;
            }
         }
         /*w -= tlp->tl_prompt_width;*/
      }
   }
   tcp_right = tccp;
   tccp = tlp->tl_line.cells + cur;

   if((f & (a_LEFT_MIN | a_RIGHT_MAX | a_HAVE_PROMPT | a_SHOW_PROMPT)) ==
            (a_LEFT_MIN | a_RIGHT_MAX | a_HAVE_PROMPT) &&
         w + tlp->tl_prompt_width <= phy_wid){
      phy_wid -= (phy_base = tlp->tl_prompt_width);
      f |= a_SHOW_PROMPT;
   }

   /* Try to avoid repainting the complete line - this is possible if the
    * cursor "did not leave the screen" and the prompt status has not changed.
    * I.e., after cramping virtual viewpoint, compare relation to physical */
   if((f & (a_TTY_VF_MOD_SINGLE/*FIXME*/ |
            a_TTY_VF_MOD_CONTENT/* xxx */ | a_TTY_VF_MOD_DIRTY)) ||
         (tcxp = tlp->tl_phy_start) == NIL || tcxp > tccp || tcxp <= tcp_right)
         f |= a_TTY_VF_MOD_DIRTY;
   else{
         f |= a_TTY_VF_MOD_DIRTY;
#if 0
         FIXME do not always repaint; carry VISIBLE_PROMPT over invocations
      struct a_tty_cell const *tcyp;
      s32 cur_displace;
      u32 phy_lmargin, phy_rmargin, fx, phy_displace;

      phy_lmargin = (fx = phy_wid) / 100;
      phy_rmargin = fx - (phy_lmargin * a_TTY_SCROLL_MARGIN_RIGHT);
      phy_lmargin *= a_TTY_SCROLL_MARGIN_LEFT;
      fx = (f & (a_SHOW_PROMPT | a_VISIBLE_PROMPT));

      if(fx == 0 || fx == (a_SHOW_PROMPT | a_VISIBLE_PROMPT)){
      }
#endif
   }

   if(f & a_TTY_VF_MAXWIDTH_POS0){
      f ^= a_TTY_VF_MAXWIDTH_POS0;
      if(f & a_RIGHT_MAX){
         f |= a_RECALC_PHY_CURSOR;
         tlp->tl_cursor = P2UZ(tcp_right + 1 - tcp_left);
      }
   }

   /* We know what we have to paint, start synchronizing */
/*jpaint:*/
   ASSERT(phy_cur == tlp->tl_phy_cursor);
   ASSERT(phy_wid == phy_wid_base - phy_base);
   ASSERT(cnt == tlp->tl_count);
   ASSERT(cnt > 0);
   ASSERT(lstcur == tlp->tl_lst_cursor);
   ASSERT(tccp == tlp->tl_line.cells + cur);

   phy_nxtcur = phy_base; /* FIXME only if repaint cpl. */

   /* Quickshot: is it only cursor movement within the visible screen? */
   if((f & a_TTY_VF_REFRESH) == a_TTY_VF_MOD_CURSOR){
      f |= a_MOVE_CURSOR;
      goto jcursor;
   }

   /* To be able to apply some quick jump offs, clear line if possible */
   if(f & a_TTY_VF_MOD_DIRTY){
      /* Force complete clearance and cursor reinitialization */
      if(!mx_termcap_cmdx(mx_TERMCAP_CMD_cr) ||
            !mx_termcap_cmd(mx_TERMCAP_CMD_ce, 0, -1))
         goto jerr;
      tlp->tl_phy_start = tcp_left;
      phy_cur = 0;
   }

   if((f & (a_TTY_VF_MOD_DIRTY | a_SHOW_PROMPT)) && phy_cur != 0){
      if(!mx_termcap_cmdx(mx_TERMCAP_CMD_cr))
         goto jerr;
      phy_cur = 0;
   }

   if(f & a_SHOW_PROMPT){
      ASSERT(phy_base == tlp->tl_prompt_width);
      if(fputs(n_string_cp(tlp->tl_prompt), mx_tty_fp) == EOF)
         goto jerr;
      phy_cur = phy_nxtcur;
   }

/* FIXME reposition cursor for paint */
   for(w = phy_nxtcur; tcp_left <= tcp_right; ++tcp_left){
      u16 cw;

      cw = tcp_left->tc_width;

      if(LIKELY(!tcp_left->tc_novis)){
         if(fwrite(tcp_left->tc_cbuf, sizeof *tcp_left->tc_cbuf,
               tcp_left->tc_count, mx_tty_fp) != tcp_left->tc_count)
            goto jerr;
      }else{ /* XXX Should not be here <-> CText, ui_str.c */
         char wbuf[8]; /* XXX magic */

         if(n_psonce & n_PSO_UNICODE){
            u32 wc;

            wc = S(u32,tcp_left->tc_wc);
            if((wc & ~0x1Fu) == 0)
               wc |= 0x2400;
            else if(wc == 0x7F)
               wc = 0x2421;
            else
               wc = 0x2426;
            su_utf32_to_8(wc, wbuf);
         }else
            wbuf[0] = '?', wbuf[1] = '\0';

         if(fputs(wbuf, mx_tty_fp) == EOF)
            goto jerr;
         cw = 1;
      }

      if(cw == U8_MAX) /* TODO yet HT == SPACE */
         cw = 1;
      w += cw;
      if(tcp_left == tccp)
         phy_nxtcur = w;
      phy_cur += cw;
   }

   /* Write something position marker alike if it does not fit on screen */
   if((f & a_HAVE_POSITION) &&
         ((f & (a_LEFT_MIN | a_RIGHT_MAX)) != (a_LEFT_MIN | a_RIGHT_MAX) /*||
          ((f & a_HAVE_PROMPT) && !(f & a_SHOW_PROMPT))*/)){
# ifdef mx_HAVE_COLOUR
      char *posbuf = tlp->tl_pos_buf, *pos = tlp->tl_pos;
# else
      char posbuf[5], *pos = posbuf;

      pos[4] = '\0';
# endif

      if(phy_cur != (w = phy_wid_base) &&
            !mx_termcap_cmd(mx_TERMCAP_CMD_ch, phy_cur = w, 0))
         goto jerr;

      *pos++ = '|';
      if(f & a_LEFT_MIN)
         su_mem_copy(pos, "^.+", 3);
      else if(f & a_RIGHT_MAX)
         su_mem_copy(pos, ".+$", 3);
      else{
         /* Theoretical line length limit a_TTY_LINE_MAX, choose next power of
          * ten (10 ** 10) to represent 100 percent; we do not have a macro
          * that generates a constant, and i do not trust the standard "u type
          * suffix automatically scales": calculate the large number */
         static char const itoa[] = "0123456789"; /* xxx inline itoa */

         u64 const fact100 = S(u64,0x3B9ACA00u) * 10u,
               fact = fact100 / 100;
         u32 i = S(u32,((fact100 / cnt) * tlp->tl_cursor) / fact);
         LCTA(a_TTY_LINE_MAX <= S32_MAX, "a_TTY_LINE_MAX too large");

         if(i < 10)
            pos[0] = ' ', pos[1] = itoa[i];
         else
            pos[1] = itoa[i % 10], pos[0] = itoa[i / 10];
         pos[2] = '%';
      }

      if(fputs(posbuf, mx_tty_fp) == EOF)
         goto jerr;
      phy_cur += 4;
   }

   /* Users are used to see the cursor right of the point of interest, so we
    * need some further adjustments unless in special conditions.  Be aware
    * that we may have adjusted cur at the beginning, too */
   if((cur = tlp->tl_cursor) == 0){
      f |= a_MOVE_CURSOR;
      phy_nxtcur = phy_base;
   }else if(f & a_RECALC_PHY_CURSOR)
      phy_nxtcur = phy_cur;
   else if(cur != cnt){
      u16 cw;

      if((cw = tccp->tc_width) == U8_MAX) /* TODO yet HT == SPACE */
         cw = 1;
      phy_nxtcur -= cw;
   }

jcursor:
   if(((f & a_MOVE_CURSOR) || phy_nxtcur != phy_cur) &&
         !mx_termcap_cmd(mx_TERMCAP_CMD_ch, phy_cur = phy_nxtcur, 0))
      goto jerr;

jleave:
   tlp->tl_lst_count = tlp->tl_count;
   tlp->tl_lst_cursor = tlp->tl_cursor;
   tlp->tl_phy_cursor = phy_cur;

   NYD2_OU;
   return ((f & a_TRUE_RV) != 0);
jerr:
   f &= ~a_TRUE_RV;
   goto jleave;
}

static s32
a_tty_wboundary(struct a_tty_line *tlp, s32 dir){/* TODO shell token-wise */
   boole anynon;
   struct a_tty_cell *tcap;
   u32 cur, cnt;
   s32 rv;
   NYD2_IN;

   ASSERT(dir == 1 || dir == -1);

   rv = -1;
   cnt = tlp->tl_count;
   cur = tlp->tl_cursor;

   if(dir < 0){
      if(cur == 0)
         goto jleave;
   }else if(cur + 1 >= cnt)
      goto jleave;
   else
      --cnt, --cur; /* xxx Unsigned wrapping may occur (twice), then */

   for(rv = 0, tcap = tlp->tl_line.cells, anynon = FAL0;;){
      wchar_t wc;

      wc = tcap[cur += S(u32,dir)].tc_wc;
      if(/*TODO not everywhere iswblank(wc)*/ wc == L' ' || wc == L'\t' ||
            iswpunct(wc)){
         if(anynon)
            break;
      }else
         anynon = TRU1;

      ++rv;

      if(dir < 0){
         if(cur == 0)
            break;
      }else if(cur + 1 >= cnt){
         ++rv;
         break;
      }
   }

jleave:
   NYD2_OU;
   return rv;
}

static void
a_tty_khome(struct a_tty_line *tlp, boole dobell){
   u32 f;
   NYD2_IN;

   if(LIKELY(tlp->tl_cursor > 0)){
      tlp->tl_cursor = 0;
      f = a_TTY_VF_MOD_CURSOR;
   }else if(dobell)
      f = a_TTY_VF_BELL;
   else
      f = a_TTY_VF_NONE;

   tlp->tl_vi_flags |= f;
   NYD2_OU;
}

static void
a_tty_kend(struct a_tty_line *tlp){
   u32 f;
   NYD2_IN;

   if(LIKELY(tlp->tl_cursor < tlp->tl_count)){
      tlp->tl_cursor = tlp->tl_count;
      f = a_TTY_VF_MOD_CURSOR;
   }else
      f = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   NYD2_OU;
}

static void
a_tty_kbs(struct a_tty_line *tlp){
   u32 f, cur, cnt;
   NYD2_IN;

   cur = tlp->tl_cursor;
   cnt = tlp->tl_count;

   if(LIKELY(cur > 0)){
      tlp->tl_cursor = --cur;
      tlp->tl_count = --cnt;

      if((cnt -= cur) > 0){
         struct a_tty_cell *tcap;

         tcap = tlp->tl_line.cells + cur;
         su_mem_move(tcap, &tcap[1], cnt *= sizeof(*tcap));
      }
      f = a_TTY_VF_MOD_CURSOR | a_TTY_VF_MOD_CONTENT;
   }else
      f = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   NYD2_OU;
}

static void
a_tty_ksnarf(struct a_tty_line *tlp, boole cplline, boole dobell){
   u32 i, f;
   NYD2_IN;

   f = a_TTY_VF_NONE;
   i = tlp->tl_cursor;

   if(cplline && i > 0){
      tlp->tl_cursor = i = 0;
      f = a_TTY_VF_MOD_CURSOR;
   }

   if(LIKELY(i < tlp->tl_count)){
      struct a_tty_cell *tcap;

      tcap = &tlp->tl_line.cells[0];
      a_tty_copy2paste(tlp, &tcap[i], &tcap[tlp->tl_count]);
      tlp->tl_count = i;
      f = a_TTY_VF_MOD_CONTENT;
   }else if(dobell)
      f |= a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   NYD2_OU;
}

static s32
a_tty_kdel(struct a_tty_line *tlp){
   u32 cur, cnt, f;
   s32 i;
   NYD2_IN;

   cur = tlp->tl_cursor;
   cnt = tlp->tl_count;
   i = S(s32,cnt - cur);

   if(LIKELY(i > 0)){
      tlp->tl_count = --cnt;

      if(LIKELY(--i > 0)){
         struct a_tty_cell *tcap;

         tcap = &tlp->tl_line.cells[cur];
         su_mem_move(tcap, &tcap[1], S(u32,i) * sizeof(*tcap));
      }
      f = a_TTY_VF_MOD_CONTENT;
   }else if(cnt == 0 && !ok_blook(ignoreeof)){
      putc('^', mx_tty_fp);
      putc('D', mx_tty_fp);
      i = -1;
      f = a_TTY_VF_NONE;
   }else{
      i = 0;
      f = a_TTY_VF_BELL;
   }

   tlp->tl_vi_flags |= f;
   NYD2_OU;
   return i;
}

static void
a_tty_kleft(struct a_tty_line *tlp){
   u32 f;
   NYD2_IN;

   if(LIKELY(tlp->tl_cursor > 0)){
      --tlp->tl_cursor;
      f = a_TTY_VF_MOD_CURSOR;
   }else
      f = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= f;
   NYD2_OU;
}

static void
a_tty_kright(struct a_tty_line *tlp){
   u32 i;
   NYD2_IN;

   if(LIKELY((i = tlp->tl_cursor + 1) <= tlp->tl_count)){
      tlp->tl_cursor = i;
      i = a_TTY_VF_MOD_CURSOR;
   }else
      i = a_TTY_VF_BELL;

   tlp->tl_vi_flags |= i;
   NYD2_OU;
}

static void
a_tty_ksnarfw(struct a_tty_line *tlp, boole fwd){
   struct a_tty_cell *tcap;
   u32 cnt, cur, f;
   s32 i;
   NYD2_IN;

   if(UNLIKELY((i = a_tty_wboundary(tlp, (fwd ? +1 : -1))) <= 0)){
      f = (i < 0) ? a_TTY_VF_BELL : a_TTY_VF_NONE;
      goto jleave;
   }

   cnt = tlp->tl_count - S(u32,i);
   cur = tlp->tl_cursor;
   if(!fwd)
      cur -= S(u32,i);
   tcap = &tlp->tl_line.cells[cur];

   a_tty_copy2paste(tlp, &tcap[0], &tcap[i]);

   if((tlp->tl_count = cnt) != (tlp->tl_cursor = cur)){
      cnt -= cur;
      su_mem_move(&tcap[0], &tcap[i], cnt * sizeof(*tcap));
   }

   f = a_TTY_VF_MOD_CURSOR | a_TTY_VF_MOD_CONTENT;
jleave:
   tlp->tl_vi_flags |= f;
   NYD2_OU;
}

static void
a_tty_kgow(struct a_tty_line *tlp, s32 dir){
   u32 f;
   s32 i;
   NYD2_IN;

   if(UNLIKELY((i = a_tty_wboundary(tlp, dir)) <= 0))
      f = (i < 0) ? a_TTY_VF_BELL : a_TTY_VF_NONE;
   else{
      if(dir < 0)
         i = -i;
      tlp->tl_cursor += S(u32,i);
      f = a_TTY_VF_MOD_CURSOR;
   }

   tlp->tl_vi_flags |= f;
   NYD2_OU;
}

static void
a_tty_kgoscr(struct a_tty_line *tlp, s32 dir){
   u32 sw, i, cur, f, cnt;
   NYD2_IN;

   if((sw = mx_termios_dimen.tiosd_width) > 2)
      sw -= 2;
   if(sw > (i = tlp->tl_prompt_width))
      sw -= i;
   cur = tlp->tl_cursor;
   f = a_TTY_VF_BELL;

   if(dir > 0){
      for(cnt = tlp->tl_count; cur < cnt && sw > 0; ++cur){
         i = tlp->tl_line.cells[cur].tc_width;
         i = MIN(sw, i);
         sw -= i;
      }
   }else{
       while(cur > 0 && sw > 0){
         i = tlp->tl_line.cells[--cur].tc_width;
         i = MIN(sw, i);
         sw -= i;
      }
   }
   if(cur != tlp->tl_cursor){
      tlp->tl_cursor = cur;
      f = a_TTY_VF_MOD_CURSOR;
   }

   tlp->tl_vi_flags |= f;
   NYD2_OU;
}

static boole
a_tty_kother(struct a_tty_line *tlp, wchar_t wc){
   /* Append if at EOL, insert otherwise;
    * since we may move around character-wise, always use a fresh ps */
   mbstate_t ps;
   struct a_tty_cell tc, *tcap;
   u32 f, cur, cnt;
   boole rv;
   NYD2_IN;

   rv = FAL0;
   f = a_TTY_VF_NONE;

   LCTA(a_TTY_LINE_MAX <= S32_MAX, "a_TTY_LINE_MAX too large");
   if(tlp->tl_count + 1 >= a_TTY_LINE_MAX){
      n_err(_("Stop here, we cannot extend line beyond size limit\n"));
      goto jleave;
   }

   /* First init a cell and see whether we'll really handle this wc */
   su_mem_set(&ps, 0, sizeof ps);
   /* C99 */{
      uz l;

      l = wcrtomb(tc.tc_cbuf, tc.tc_wc = wc, &ps);
      if(UNLIKELY(l > MB_LEN_MAX)){
jemb:
         n_err(_("wcrtomb(3) error: too many multibyte character bytes\n"));
         goto jleave;
      }
      tc.tc_count = S(u16,l);

      if(UNLIKELY((n_psonce & n_PSO_ENC_MBSTATE) != 0)){
         l = wcrtomb(&tc.tc_cbuf[l], L'\0', &ps);
         if(LIKELY(l == 1))
            /* Only NUL terminator */;
         else if(LIKELY(--l < MB_LEN_MAX))
            tc.tc_count += S(u16,l);
         else
            goto jemb;
      }
   }

   /* Yes, we will!  Place it in the array */
   tc.tc_novis = (iswprint(wc) == 0);
   tc.tc_width = a_tty_wcwidth(wc);
   /* TODO if(tc.tc_novis && tc.tc_width > 0) */

   cur = tlp->tl_cursor++;
   cnt = tlp->tl_count++ - cur;
   tcap = &tlp->tl_line.cells[cur];
   if(cnt >= 1){
      su_mem_move(&tcap[1], tcap, cnt * sizeof(*tcap));
      f = a_TTY_VF_MOD_CONTENT;
   }else
      f = a_TTY_VF_MOD_SINGLE;
   su_mem_copy(tcap, &tc, sizeof *tcap);

   f |= a_TTY_VF_MOD_CURSOR;
   rv = TRU1;
jleave:
   if(!rv)
      f |= a_TTY_VF_BELL;
   tlp->tl_vi_flags |= f;

   NYD2_OU;
   return rv;
}

static u32
a_tty_kht(struct a_tty_line *tlp){
   struct su_mem_bag *membag, *membag_persist, membag__buf[1];
   struct stat sb;
   struct str orig, bot, topp, sub, exp, preexp;
   struct n_string shou, *shoup;
   char **exp_res;
   struct a_tty_cell *ctop, *cx;
   boole wedid, set_savec;
   u32 rv, f;
   NYD2_IN;

   /* Get plain line data; if this is the first expansion/xy, update the
    * very original content so that ^G gets the origin back */
   orig = tlp->tl_savec;
   a_tty_cell2save(tlp);
   exp = tlp->tl_savec;
   if(orig.s != NIL){
      /*tlp->tl_savec = orig;*/
      set_savec = FAL0;
   }else
      set_savec = TRU1;
   orig = exp;

   membag = su_mem_bag_create(&membag__buf[0], 0);
   membag_persist = su_mem_bag_top(mx_go_data->gdc_membag);
   su_mem_bag_push(mx_go_data->gdc_membag, membag);

   shoup = n_string_creat_auto(&shou);
   f = a_TTY_VF_NONE;

   /* C99 */{
      uz max;
      struct a_tty_cell *cword;

      /* Find the word to be expanded */
      cword = tlp->tl_line.cells;
      ctop = &cword[tlp->tl_cursor];
      cx = &cword[tlp->tl_count];

      /* topp: separate data right of cursor */
      if(cx > ctop){
         for(rv = 0; ctop < cx; ++ctop)
            rv += ctop->tc_count;
         topp.l = rv;
         topp.s = orig.s + orig.l - rv;
         ctop = cword + tlp->tl_cursor;
      }else
         topp.s = NIL, topp.l = 0;

      /* Find the shell token that corresponds to the cursor position */
      max = 0;
      if(ctop > cword){
         for(; cword < ctop; ++cword)
            max += cword->tc_count;
      }
      bot = sub = orig;
      bot.l = 0;
      sub.l = max;

      if(max > 0){
         for(;;){
            BITENUM_IS(u32,n_shexp_state) shs;

            exp = sub;
            shs = n_shexp_parse_token((n_SHEXP_PARSE_DRYRUN |
                  n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_IGNORE_EMPTY |
                  n_SHEXP_PARSE_QUOTE_AUTO_CLOSE), NIL, &sub, NIL);
            if(sub.l != 0){
               uz x;

               ASSERT(max >= sub.l);
               x = max - sub.l;
               bot.l += x;
               max -= x;
               continue;
            }
            if(shs & n_SHEXP_STATE_ERR_MASK){
               n_err(_("Invalid completion pattern: %.*s\n"),
                  S(int,exp.l), exp.s);
               f |= a_TTY_VF_BELL;
               goto jnope;
            }

            /* All WS?  Trailing WS that has been "jumped over"? */
            if(exp.l == 0 || (shs & n_SHEXP_STATE_WS_TRAIL))
               break;

            n_shexp_parse_token((n_SHEXP_PARSE_TRIM_SPACE |
                  n_SHEXP_PARSE_IGNORE_EMPTY | n_SHEXP_PARSE_QUOTE_AUTO_CLOSE),
                  shoup, &exp, NIL);
            break;
         }

         sub.s = n_string_cp(shoup);
         sub.l = shoup->s_len;
      }
   }

   /* Leave room for "implicit asterisk" expansion, as below */
   if(sub.l == 0){
      sub.s = UNCONST(char*,n_star);
      sub.l = sizeof(n_star) -1;
   }

   preexp.s = UNCONST(char*,su_empty);
   preexp.l = sizeof(su_empty) -1;
   wedid = FAL0;
jredo:
   /* TODO Super-Heavy-Metal: block all sigs, avoid leaks on jump */
   mx_sigs_all_holdx();
   exp_res = mx_shexp_name_expand_multi(sub.s, a_TTY_TAB_FEXP_FL);
   mx_sigs_all_rele();

   if(exp_res == NIL || exp_res[0][0] == '\0'){
      if(wedid < FAL0)
         goto jnope;

      /* No.  But maybe the users' desire was to complete only a part of the
       * shell token of interest!  TODO This can be improved, we would need to
       * TODO have shexp_parse to create a DOM structure of parsed snippets, so
       * TODO that we can tell for each snippet which quote is active and
       * TODO whether we may cross its boundary and/or apply expansion for it!
       * TODO Only like that we would be able to properly requote user input
       * TODO like "'['a-z]<TAB>" to e.g. "\[a-z]" for glob purposes!
       * TODO Then, honour *line-editor-word-breaks* from ground up! */
      if(wedid == TRU1){
         uz i, li;
         char const *word_breaks;

         wedid = TRUM1;

         word_breaks = ok_vlook(line_editor_cpl_word_breaks);
         li = UZ_MAX;
         i = sub.l;

         while(i-- != 0){
            char c;

            c = sub.s[i];
            if(su_cs_is_space(c))
               break;
            else if((i != sub.l - 1 || c != '*') &&
                  su_cs_find_c(word_breaks, c) != NIL){
               li = i + 1;
               break;
            }
         }
         if(li == UZ_MAX)
            goto jnope;

         preexp = sub;
         preexp.l = li;
         sub.l -= li;
         sub.s += li;
         goto jredo;
      }

      /* A different case is that the user input includes for example character
       * classes: here fexpand() will go over glob, and that will not find any
       * match, thus returning NIL; try to wildcard expand this pattern! */
jaster_check:
      if(sub.s[sub.l - 1] != '*'){
         wedid = TRU1;
         shoup = n_string_push_c(shoup, '*');
         sub.s = n_string_cp(shoup);
         sub.l = shoup->s_len;
         goto jredo;
      }
      goto jnope;
   }

   if(wedid == TRUM1 && preexp.l > 0)
      preexp.s = savestrbuf(preexp.s, preexp.l);

   /* May be multi-return! */
   if(exp_res[1] != NIL)
      goto jmulti;

   exp.l = su_cs_len(exp.s = exp_res[0]);

   /* xxx That not really true since the limit counts characters not bytes */
   LCTA(a_TTY_LINE_MAX <= S32_MAX, "a_TTY_LINE_MAX too large");
   if(exp.l >= a_TTY_LINE_MAX - 1 || a_TTY_LINE_MAX - 1 - exp.l < preexp.l){
      n_err(_("Tabulator expansion would extend beyond line size limit\n"));
      f |= a_TTY_VF_BELL;
      goto jnope;
   }

   /* If the expansion equals the original string, assume the user wants what
    * is usually known as tab completion, append `*' and restart */
   if(!wedid && exp.l == sub.l && !su_mem_cmp(exp.s, sub.s, exp.l))
      goto jaster_check;

   if(exp.s[exp.l - 1] != '/'){
      if(!stat(exp.s, &sb) && S_ISDIR(sb.st_mode)){
         shoup = n_string_assign_buf(shoup, exp.s, exp.l);
         shoup = n_string_push_c(shoup, '/');
         exp.s = n_string_cp(shoup);
         goto jset;
      }
   }
   exp.s[exp.l] = '\0';

jset:
   exp.l = su_cs_len(exp.s = n_shexp_quote_cp(exp.s,
            ((tlp->tl_conf_flags & a_TTY_CONF_QUOTE_RNDTRIP) != 0)));
   tlp->tl_defc_cursor_byte = bot.l + preexp.l + exp.l -1;

   orig.l = bot.l + preexp.l + exp.l + topp.l;
   su_mem_bag_push(mx_go_data->gdc_membag, membag_persist);
   orig.s = su_MEM_BAG_SELF_AUTO_ALLOC(orig.l + 5 +1);
   su_mem_bag_pop(mx_go_data->gdc_membag, membag_persist);
   if((rv = S(u32,bot.l)) > 0)
      su_mem_copy(orig.s, bot.s, rv);
   if(preexp.l > 0){
      su_mem_copy(&orig.s[rv], preexp.s, preexp.l);
      rv += preexp.l;
   }
   su_mem_copy(&orig.s[rv], exp.s, exp.l);
   rv += exp.l;
   if(topp.l > 0){
      su_mem_copy(&orig.s[rv], topp.s, topp.l);
      rv += topp.l;
   }
   orig.s[rv] = '\0';

   tlp->tl_defc = orig;
   tlp->tl_count = tlp->tl_cursor = 0;
   f |= a_TTY_VF_MOD_DIRTY;
jleave:
   su_mem_bag_pop(mx_go_data->gdc_membag, membag);
   su_mem_bag_gut(membag);
   tlp->tl_vi_flags |= f;

   NYD2_OU;
   return rv;

jmulti:{
      struct mx_visual_info_ctx vic;
      struct str input;
      wc_t c2, c1;
      char const *lococp;
      uz idx, locolen, scrwid, lnlen, lncnt, prefixlen;
      FILE *fp;

      if((fp = mx_fs_tmp_open(NIL, "mlecpl", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
               NIL)) == NIL){
         n_perr(_("tmpfile"), 0);
         fp = mx_tty_fp;
      }

      shoup = n_string_reserve(n_string_trunc(shoup, 0), 80 -1);

      /* Iterate (once again) over all results */
      scrwid = mx_TERMIOS_WIDTH_OF_LISTS();
      lnlen = lncnt = 0;
      UNINIT(locolen, 0);
      UNINIT(prefixlen, 0);
      UNINIT(lococp, NIL);
      UNINIT(c1, '\0');
      for(idx = 0; (sub.s = exp_res[idx]) != NIL; c1 = c2, ++idx){
         char const *fullpath;

         sub.l = su_cs_len(sub.s);

         /* Separate dirname and basename */
         fullpath = sub.s;
         if(idx == 0){
            char const *cp;

            if((cp = su_cs_rfind_c(fullpath, '/')) != NIL)
               prefixlen = P2UZ(++cp - fullpath);
            else
               prefixlen = 0;
         }
         if(prefixlen > 0 && prefixlen < sub.l){
            sub.l -= prefixlen;
            sub.s += prefixlen;
         }

         /* We want case-insensitive sort-order */
         su_mem_set(&vic, 0, sizeof vic);
         vic.vic_indat = sub.s;
         vic.vic_inlen = sub.l;
         c2 = mx_visual_info(&vic, mx_VISUAL_INFO_ONE_CHAR) ? vic.vic_waccu
               : S(u8,*sub.s);
#ifdef mx_HAVE_C90AMEND1
         c2 = towlower(c2);
#else
         c2 = su_cs_to_lower(c2);
#endif

         /* Query longest common prefix along the way */
         if(idx == 0){
            c1 = c2;
            lococp = sub.s;
            locolen = sub.l;
         }else if(locolen > 0){
            uz i;

            for(i = 0; i < locolen; ++i)
               if(lococp[i] != sub.s[i]){
                  i = mx_field_detect_clip(i, lococp, i);
                  locolen = i;
                  break;
               }
         }

         /* Prepare display */
         input = sub;
         shoup = n_shexp_quote(n_string_trunc(shoup, 0), &input,
               ((tlp->tl_conf_flags & a_TTY_CONF_QUOTE_RNDTRIP) != 0));
         su_mem_set(&vic, 0, sizeof vic);
         vic.vic_indat = shoup->s_dat;
         vic.vic_inlen = shoup->s_len;
         if(!mx_visual_info(&vic,
               mx_VISUAL_INFO_SKIP_ERRORS | mx_VISUAL_INFO_WIDTH_QUERY))
            vic.vic_vi_width = shoup->s_len;

         /* Put on screen.  Indent follow lines of same sort slot.
          * Leave enough room for filename tagging */
         if((c1 = (c1 != c2))){
#ifdef mx_HAVE_C90AMEND1
            c1 = (iswalnum(c2) != 0);
#else
            c1 = (su_cs_is_alnum(c2) != 0);
#endif
         }
         if(idx == 0 || c1 ||
               scrwid < lnlen || scrwid - lnlen <= vic.vic_vi_width + 2){
            putc('\n', fp);
            if(scrwid < lnlen)
               ++lncnt;
            ++lncnt, lnlen = 0;
            if(idx != 0 && !c1)
               goto jsep;
         }else if(lnlen > 0){
jsep:
            fputs("  ", fp);
            lnlen += 2;
         }
         fputs(n_string_cp(shoup), fp);
         lnlen += vic.vic_vi_width;

         /* Support the known filename tagging
          * XXX *line-editor-completion-filetype* or so */
         if(!lstat(fullpath, &sb)){
            char c = '\0';

            if(S_ISDIR(sb.st_mode))
               c = '/';
            else if(S_ISLNK(sb.st_mode))
               c = '@';
# ifdef S_ISFIFO
            else if(S_ISFIFO(sb.st_mode))
               c = '|';
# endif
# ifdef S_ISSOCK
            else if(S_ISSOCK(sb.st_mode))
               c = '=';
# endif
# ifdef S_ISCHR
            else if(S_ISCHR(sb.st_mode))
               c = '%';
# endif
# ifdef S_ISBLK
            else if(S_ISBLK(sb.st_mode))
               c = '#';
# endif

            if(c != '\0'){
               putc(c, fp);
               ++lnlen;
            }
         }
      }
      putc('\n', fp);
      ++lncnt;

      if(fp != mx_tty_fp){
         su_mem_bag_push(mx_go_data->gdc_membag, membag_persist);
         page_or_print(fp, lncnt);
         su_mem_bag_pop(mx_go_data->gdc_membag, membag_persist);

         mx_fs_close(fp);
      }

      n_string_gut(shoup);

      /* A common prefix of 0 means we cannot provide the user any auto
       * completed characters for the multiple possible results.
       * Otherwise we can, so extend the visual line content by the common
       * prefix (in a reversible way) */
      f |= a_TTY_VF_BELL; /* XXX -> *line-editor-completion-bell*? or so */
      if(locolen > 0){
         (exp.s = UNCONST(char*,lococp))[locolen] = '\0';
         exp.s -= prefixlen;
         exp.l = (locolen += prefixlen);
         goto jset;
      }
   }

jnope:
   /* If we have provided a default content, but failed to expand, there is
    * nothing we can "revert to": drop that default again */
   if(set_savec){
      tlp->tl_savec.s = NIL;
      tlp->tl_savec.l = 0;
   }
   f &= a_TTY_VF_BELL; /* XXX -> *line-editor-completion-bell*? or so */
   rv = 0;
   goto jleave;
}

# ifdef mx_HAVE_HISTORY
static u32
a_tty__khist_shared(struct a_tty_line *tlp, struct a_tty_hist *thp){
   u32 f, rv;
   NYD2_IN;

   if(LIKELY((tlp->tl_hist = thp) != NIL)){
      char *cp;
      uz i;

      i = thp->th_len;
      if((tlp->tl_goinflags & mx__GO_INPUT_CTX_MASK
            ) == mx_GO_INPUT_CTX_COMPOSE){
         ++i;
         if(!(thp->th_flags & a_TTY_HIST_CTX_COMPOSE))
            ++i;
      }

      tlp->tl_defc.s = cp = su_AUTO_ALLOC(i + 2 +1);

      /* Compose mode is tricky and not yet truly supported TODO
       * TODO .. in that only non-compose mode commands will work */
      if((tlp->tl_goinflags & mx__GO_INPUT_CTX_MASK
            ) == mx_GO_INPUT_CTX_COMPOSE){
         if((*cp = ok_vlook(escape)[0]) == '\0')
            *cp = n_ESCAPE[0];
         ++cp;
         if(!(thp->th_flags & a_TTY_HIST_CTX_COMPOSE)){
            *cp++ = ':';
            *cp++ = ' ';
         }
      }
      su_mem_copy(cp, thp->th_dat, thp->th_len +1);
      rv = tlp->tl_defc.l = i;

      f = (tlp->tl_count > 0) ? a_TTY_VF_MOD_DIRTY : a_TTY_VF_NONE;
      if(tlp->tl_conf_flags & a_TTY_CONF_SRCH_POS0)
         f |= a_TTY_VF_MAXWIDTH_POS0;
      tlp->tl_count = tlp->tl_cursor = 0;
   }else{
      f = a_TTY_VF_BELL;
      rv = U32_MAX;
   }

   tlp->tl_vi_flags |= f;

   NYD2_OU;
   return rv;
}

static u32
a_tty_khist(struct a_tty_line *tlp, boole fwd){
   struct a_tty_hist *thp;
   u32 rv;
   NYD2_IN;

   /* If we are not in history mode yet, save line content */
   if((thp = tlp->tl_hist) == NIL){
      a_tty_cell2save(tlp);
      if((thp = a_tty.tg_hist) == NIL)
         goto jleave;
      if(fwd)
         while(thp->th_older != NIL)
            thp = thp->th_older;
      goto jleave;
   }

   while((thp = fwd ? thp->th_younger : thp->th_older) != NIL){
      /* Applicable to input context?  Compose mode swallows anything */
      if((tlp->tl_goinflags & mx__GO_INPUT_CTX_MASK
            ) == mx_GO_INPUT_CTX_COMPOSE)
         break;
      if((thp->th_flags & a_TTY_HIST_CTX_MASK) != a_TTY_HIST_CTX_COMPOSE)
         break;
   }

jleave:
   rv = a_tty__khist_shared(tlp, thp);

   NYD2_OU;
   return rv;
}

static u32
a_tty_khist_search(struct a_tty_line *tlp, boole fwd){
# ifdef mx_HAVE_REGEX
   struct su_re re;
# endif
   struct str orig_savec;
   u32 xoff, rv;
   struct a_tty_hist *thp;
   NYD2_IN;

   thp = NIL;
# ifdef mx_HAVE_REGEX
   if(tlp->tl_conf_flags & a_TTY_CONF_SRCH_REGEX)
      su_re_create(&re);
# endif

   /* We cannot complete an empty line */
   if(UNLIKELY(tlp->tl_count == 0)){
      /* XXX The upcoming hard reset would restore a set savec buffer,
       * XXX so forcefully reset that.  A cleaner solution would be to
       * XXX reset it whenever a restore is no longer desired */
      tlp->tl_savec.s = NIL;
      tlp->tl_savec.l = 0;
      goto jleave;
   }

   /* xxx It is a bit of a hack, but let us just hard-code the knowledge that
    * xxx in compose mode the first character is *escape* and must be skipped
    * xxx when searching the history.  Alternatively we would need to have
    * xxx context-specific history search hooks which would do the search,
    * xxx which is overkill for our sole special case compose mode */
   if((tlp->tl_goinflags & mx__GO_INPUT_CTX_MASK) == mx_GO_INPUT_CTX_COMPOSE)
      xoff = 1;
   else
      xoff = 0;

   if((thp = tlp->tl_hist) == NIL){
      a_tty_cell2save(tlp);
      if((thp = a_tty.tg_hist) == NIL) /* TODO Should end "doing nothing"! */
         goto jleave;
      if(fwd)
         while(thp->th_older != NIL)
            thp = thp->th_older;
      orig_savec.s = NIL;
      orig_savec.l = 0; /* silence CC */
   }else{
      while((thp = fwd ? thp->th_younger : thp->th_older) != NIL){
         /* Applicable to input context?  Compose mode swallows anything */
         if(xoff != 0)
            break;
         if((thp->th_flags & a_TTY_HIST_CTX_MASK) != a_TTY_HIST_CTX_COMPOSE)
            break;
      }
      if(thp == NIL)
         goto jleave;

      orig_savec = tlp->tl_savec;
   }

   if(xoff >= tlp->tl_savec.l){
      thp = NIL;
      goto jleave;
   }

   if(orig_savec.s == NIL)
      a_tty_cell2save(tlp);

# ifdef mx_HAVE_REGEX
   if((tlp->tl_conf_flags & a_TTY_CONF_SRCH_REGEX) &&
         su_re_setup_cp(&re, &tlp->tl_savec.s[xoff],
            (su_RE_SETUP_EXT | su_RE_SETUP_TEST_ONLY |
             ((tlp->tl_conf_flags & a_TTY_CONF_SRCH_CASE) ? su_RE_SETUP_ICASE
              : su_RE_SETUP_NONE))) != su_RE_ERROR_NONE){
      n_err(_("Invalid regular expression: %s: %s\n"),
         n_shexp_quote_cp(&tlp->tl_savec.s[xoff], FAL0),
         su_re_error_doc(&re));
      thp = NIL;
      goto jleave;
   }
# endif

   for(; thp != NIL; thp = fwd ? thp->th_younger : thp->th_older){
      /* Applicable to input context?  Compose mode swallows anything */
      if(xoff != 1 && (thp->th_flags & a_TTY_HIST_CTX_MASK) ==
            a_TTY_HIST_CTX_COMPOSE)
         continue;
# ifdef mx_HAVE_REGEX
      else if(tlp->tl_conf_flags & a_TTY_CONF_SRCH_REGEX){
         if(su_re_eval_cp(&re, thp->th_dat, su_RE_EVAL_NONE))
            break;
      }
# endif
      else{
         char *cp;

         cp = ((tlp->tl_conf_flags & a_TTY_CONF_SRCH_CASE)
                  ? mx_substr : su_cs_find)
               (thp->th_dat, &tlp->tl_savec.s[xoff]);
         if(cp != thp->th_dat && !(tlp->tl_conf_flags & a_TTY_CONF_SRCH_ANY))
            cp = NIL;
         if(cp != NIL)
            break;
      }
   }

   if(orig_savec.s != NIL)
      tlp->tl_savec = orig_savec;

jleave:
# ifdef mx_HAVE_REGEX
   if(tlp->tl_conf_flags & a_TTY_CONF_SRCH_REGEX)
      su_re_gut(&re);
# endif

   rv = a_tty__khist_shared(tlp, thp);

   NYD2_OU;
   return rv;
}
# endif /* mx_HAVE_HISTORY */

static enum a_tty_fun_status
a_tty_fun(struct a_tty_line *tlp, enum a_tty_bind_flags tbf, uz *len){
   enum a_tty_fun_status rv;
   NYD2_IN;

   rv = a_TTY_FUN_STATUS_OK;
# undef a_X
# define a_X(N) a_TTY_BIND_FUN_REDUCE(a_TTY_BIND_FUN_ ## N)
   switch(a_TTY_BIND_FUN_REDUCE(tbf)){
   case a_X(BELL):
      tlp->tl_vi_flags |= a_TTY_VF_BELL;
      break;
   case a_X(GO_BWD):
      a_tty_kleft(tlp);
      break;
   case a_X(GO_FWD):
      a_tty_kright(tlp);
      break;
   case a_X(GO_WORD_BWD):
      a_tty_kgow(tlp, -1);
      break;
   case a_X(GO_WORD_FWD):
      a_tty_kgow(tlp, +1);
      break;
   case a_X(GO_SCREEN_BWD):
      a_tty_kgoscr(tlp, -1);
      break;
   case a_X(GO_SCREEN_FWD):
      a_tty_kgoscr(tlp, +1);
      break;
   case a_X(GO_HOME):
      a_tty_khome(tlp, TRU1);
      break;
   case a_X(GO_END):
      a_tty_kend(tlp);
      break;
   case a_X(DEL_BWD):
      a_tty_kbs(tlp);
      break;
   case a_X(DEL_FWD):
      if(a_tty_kdel(tlp) < 0)
         rv = a_TTY_FUN_STATUS_END;
      break;
   case a_X(SNARF_WORD_BWD):
      a_tty_ksnarfw(tlp, FAL0);
      break;
   case a_X(SNARF_WORD_FWD):
      a_tty_ksnarfw(tlp, TRU1);
      break;
   case a_X(SNARF_END):
      a_tty_ksnarf(tlp, FAL0, TRU1);
      break;
   case a_X(SNARF_LINE):
      a_tty_ksnarf(tlp, TRU1, (tlp->tl_count == 0));
      break;

   case a_X(HIST_FWD):
# ifdef mx_HAVE_HISTORY
   /* C99 */{
         boole isfwd = TRU1;

         if(0){
# endif
      /* FALLTHRU */
   case a_X(HIST_BWD):
# ifdef mx_HAVE_HISTORY
            isfwd = FAL0;
         }
         if((*len = a_tty_khist(tlp, isfwd)) != U32_MAX){
            rv = a_TTY_FUN_STATUS_RESTART;
            break;
         }
         goto jreset;
      }
# endif
      tlp->tl_vi_flags |= a_TTY_VF_BELL;
      break;

   case a_X(HIST_SRCH_FWD):{
# ifdef mx_HAVE_HISTORY
      boole isfwd = TRU1;

      if(0){
# endif
      /* FALLTHRU */
   case a_X(HIST_SRCH_BWD):
# ifdef mx_HAVE_HISTORY
         isfwd = FAL0;
      }
      if((*len = a_tty_khist_search(tlp, isfwd)) != U32_MAX){
         rv = a_TTY_FUN_STATUS_RESTART;
         break;
      }
      goto jreset;
# else
      tlp->tl_vi_flags |= a_TTY_VF_BELL;
# endif
      }break;

   case a_X(REPAINT):
      tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY;
      break;
   case a_X(QUOTE_RNDTRIP):
      tlp->tl_conf_flags ^= a_TTY_CONF_QUOTE_RNDTRIP;
      break;
   case a_X(PROMPT_CHAR):{
      wchar_t wc;

      if((wc = a_tty_vinuni(tlp)) > 0)
         a_tty_kother(tlp, wc);
      }break;
   case a_X(COMPLETE):
      if((*len = a_tty_kht(tlp)) > 0)
         rv = a_TTY_FUN_STATUS_RESTART;
      break;

   case a_X(PASTE):
      if(tlp->tl_pastebuf.l > 0){
         *len = (tlp->tl_defc = tlp->tl_pastebuf).l;
         rv = a_TTY_FUN_STATUS_RESTART;
      }else
         tlp->tl_vi_flags |= a_TTY_VF_BELL;
      break;

   case a_X(CLEAR_SCREEN):
      tlp->tl_vi_flags |= (mx_termcap_cmdx(mx_TERMCAP_CMD_cl) == TRU1)
            ? a_TTY_VF_MOD_DIRTY : a_TTY_VF_BELL;
      break;

   case a_X(RAISE_INT):
#ifdef SIGINT
      n_raise(SIGINT);
#endif
      break;
   case a_X(RAISE_QUIT):
#ifdef SIGTSTP
      n_raise(SIGQUIT);
#endif
      break;
   case a_X(RAISE_TSTP):
#ifdef SIGTSTP
      n_raise(SIGTSTP);
#endif
      break;

   case a_X(CANCEL):
      /* Normally this just causes a restart and thus resets the state
       * machine  */
      if(tlp->tl_savec.l == 0 && tlp->tl_defc.l == 0){
      }
# ifdef mx_HAVE_KEY_BINDINGS
      tlp->tl_bind_takeover = '\0';
# endif
      tlp->tl_vi_flags |= a_TTY_VF_BELL;
      rv = a_TTY_FUN_STATUS_RESTART;
      break;

   case a_X(RESET):
      if(tlp->tl_count == 0 && tlp->tl_savec.l == 0 && tlp->tl_defc.l == 0){
# ifdef mx_HAVE_KEY_BINDINGS
         tlp->tl_bind_takeover = '\0';
# endif
         tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY | a_TTY_VF_BELL;
         break;
      }else if(0){
   case a_X(FULLRESET):
         tlp->tl_savec.s = tlp->tl_defc.s = NIL;
         tlp->tl_savec.l = tlp->tl_defc.l = 0;
         tlp->tl_defc_cursor_byte = 0;
         tlp->tl_vi_flags |= a_TTY_VF_BELL;
      }
jreset:
# ifdef mx_HAVE_KEY_BINDINGS
      tlp->tl_bind_takeover = '\0';
# endif
      tlp->tl_vi_flags |= a_TTY_VF_MOD_DIRTY;
      tlp->tl_cursor = tlp->tl_count = 0;
# ifdef mx_HAVE_HISTORY
      tlp->tl_hist = NIL;
# endif
      if((*len = tlp->tl_savec.l) != 0){
         tlp->tl_defc = tlp->tl_savec;
         tlp->tl_savec.s = NIL;
         tlp->tl_savec.l = 0;
      }else
         *len = tlp->tl_defc.l;
      rv = a_TTY_FUN_STATUS_RESTART;
      break;

   default:
   case a_X(COMMIT):
      rv = a_TTY_FUN_STATUS_COMMIT;
      break;
   }
# undef a_X

   NYD2_OU;
   return rv;
}

static sz
a_tty_readline(struct a_tty_line *tlp, uz len, boole *histok_or_nil
      su_DBG_LOC_ARGS_DECL){
   /* We want to save code, yet we may have to incorporate a lines'
    * default content and / or default input to switch back to after some
    * history movement; let "len > 0" mean "have to display some data
    * buffer", and only otherwise read(2) it */
# ifdef mx_HAVE_KEY_BINDINGS
   struct a_tty_bind_tree *tbtp;
   boole timeout;
# endif
   mbstate_t ps[2];
   char cbuf_base[MB_LEN_MAX * 2], *cbuf, *cbufp;
   wchar_t wc;
   BITENUM_IS(u32,a_tty_bind_flags) tbf;
   sz rv;
   NYD_IN;

   UNINIT(rv, 0);
# ifdef mx_HAVE_KEY_BINDINGS
   ASSERT(tlp->tl_bind_takeover == '\0');
# endif

jrestart:
   su_mem_set(ps, 0, sizeof ps);
   tlp->tl_vi_flags |= a_TTY_VF_REFRESH | a_TTY_VF_SYNC;

   /* Treat buffer take-over mode specially, that simplifies the below */
   if(UNLIKELY(len != 0)){
      /* Ensure we have valid pointers, and room for grow */
      a_tty_check_grow(tlp, S(u32,len)  su_DBG_LOC_ARGS_USE);

      for(;;){
         ASSERT(tlp->tl_defc.l > 0 && tlp->tl_defc.s != NIL);
         ASSERT(tlp->tl_defc.l >= len);
         cbufp =
            cbuf = tlp->tl_defc.s + (tlp->tl_defc.l - len);
         cbufp += len;

         rv = S(sz,mbrtowc(&wc, cbuf, P2UZ(cbufp - cbuf), &ps[0]));
         if(rv <= 0){
            /* Error during buffer take-over can only result in a hard reset */
            a_tty_fun(tlp, a_TTY_BIND_FUN_FULLRESET, &len);
            goto jrestart;
         }

         if(a_tty_kother(tlp, wc)){
            /* ?? */
         }

         if((len -= S(uz,rv)) == 0){
            /* Buffer mode completed */
            tlp->tl_defc.s = NIL;
            tlp->tl_defc.l = 0;
            break;
         }

         /* Ensure more room -- should normally be a no-op */
         a_tty_check_grow(tlp, 1  su_DBG_LOC_ARGS_USE);
      }
      goto jrestart;
   }

jinput_loop:
   for(;;){
# ifdef mx_HAVE_KEY_BINDINGS
      enum a_tty_term_timeout_mode ttm;
      struct inseq{
         struct inseq *last;
         struct inseq *next;
         struct a_tty_bind_tree *tbtp;
      } *isp_head, *isp;

      isp_head = isp = NIL;
      UNINIT(ttm, a_TTY_TTM_NONE);
# endif

      /* Handle visual state flags */
      if(tlp->tl_vi_flags & a_TTY_VF_ALL_MASK)
         if(!a_tty_vi_refresh(tlp)){
            rv = -1;
            goto jleave;
         }

      cbufp = cbuf = cbuf_base;

      for(;;){
         /* Ensure we have valid pointers, and room for grow */
         a_tty_check_grow(tlp, 1  su_DBG_LOC_ARGS_USE);

# ifdef mx_HAVE_KEY_BINDINGS
         timeout = FAL0;

         if(tlp->tl_bind_takeover != '\0'){
            wc = tlp->tl_bind_takeover;
            tlp->tl_bind_takeover = '\0';
         }else
# endif
         {
            /* Let me at least once dream of iomon(itor), timer with
             * one-shot, enwrapped with key_event and key_sequence_event,
             * all driven by an event_loop */
# ifdef mx_HAVE_KEY_BINDINGS
jread_again:
            if(isp == NIL){
               timeout = FAL0;
               ttm = a_TTY_TTM_NONE;
            }else{
               /* If the current isp has children with and without ismbseq
                * then we will restart waiting for the normal key.
                * Otherwise check which timeout to use at first */
               if(timeout){
                  ASSERT(ttm == a_TTY_TTM_MBSEQ);
                  ttm = a_TTY_TTM_KEY_AFTER_MBSEQ;
               }else{
                  ASSERT(!timeout);
                  ttm = a_TTY_TTM_KEY;
                  for(tbtp = isp->tbtp->tbt_children; tbtp != NIL;
                        tbtp = tbtp->tbt_sibling)
                     if(tbtp->tbt_ismbseq){
                        ttm = a_TTY_TTM_MBSEQ;
                        break;
                     }
               }

               timeout = TRU1;
               a_tty_term_rawmode_timeout(tlp, ttm);
            }
# endif /* mx_HAVE_KEY_BINDINGS */

            while((rv = read(STDIN_FILENO, cbufp, 1)) == -1){
               /* TODO Currently a noop due to SA_RESTART */
               if(su_err_no() != su_ERR_INTR ||
                     ((tlp->tl_vi_flags & a_TTY_VF_MOD_DIRTY) &&
                      !a_tty_vi_refresh(tlp)))
                  break;
            }

# ifdef mx_HAVE_KEY_BINDINGS
            if(timeout)
               a_tty_term_rawmode_timeout(tlp, a_TTY_TTM_NONE);
# endif

            if(rv < 0)
               goto jleave;
# ifdef mx_HAVE_KEY_BINDINGS
            /* Timeout expiration */
            else if(rv == 0){
               ASSERT(timeout);
               ASSERT(isp != NIL);

               /* If we were waiting for a tbt_ismbseq to be continued, maybe
                * there is a normal key child also */
               if(ttm == a_TTY_TTM_MBSEQ){
                  for(tbtp = isp->tbtp->tbt_children; tbtp != NIL;
                        tbtp = tbtp->tbt_sibling)
                     if(!tbtp->tbt_ismbseq)
                        goto jread_again;
               }

               /* Maybe that can be terminated here? */
               if((tbtp = isp->tbtp)->tbt_bind != NIL){
                  tlp->tl_bind_takeover = wc;
                  goto jhave_bind;
               }

               /* Key timeout, take over all sequence bytes as-is */
               wc = '\0';
               goto jtake_over;
            }
# endif /* mx_HAVE_KEY_BINDINGS */

            /* As a special case, simulate EOF via EOT (which can happen via
             * type-ahead, for example when typing "yes\n^@" during sleep of
             *    $ sleep 5; mail -s byjove $LOGNAME */
            if(*cbufp == '\0'){
               ASSERT((n_psonce & n_PSO_INTERACTIVE) &&
                  !(n_pstate & n_PS_ROBOT));
               *cbuf = '\x04';
            }
            ++cbufp;

            rv = S(sz,mbrtowc(&wc, cbuf, P2UZ(cbufp - cbuf), &ps[0]));
            if(rv <= 0){
               /* If it is a hard error, or if too many redundant shift
                * sequences overflow our buffer: perform hard reset */
               if(rv == -1 || sizeof cbuf_base == P2UZ(cbufp - cbuf)){
                  a_tty_fun(tlp, a_TTY_BIND_FUN_FULLRESET, &len);
                  goto jrestart;
               }
               /* Otherwise, due to the way we deal with the buffer, we need
                * to restore the mbstate_t from before this conversion */
               ps[0] = ps[1];
               continue;
            }
            cbufp = cbuf;
            ps[1] = ps[0];
         }

         /* We have read a complete (multibyte) character into wc. */

# ifdef mx_HAVE_KEY_BINDINGS
         timeout = FAL0;

         /* Check for special bypass input function first */
         if(su_cs_is_ascii(wc)){
            char const *cp;
            char c;

            for(c = S(char,wc), cp = &(*tlp->tl_bind_shcut_prompt_char)[0];
                  *cp != '\0'; ++cp)
               if(c == *cp){
                  wc = a_tty_vinuni(tlp);
                  break;
               }
            if(wc == '\0'){
               tlp->tl_vi_flags |= a_TTY_VF_BELL;
               goto jinput_loop;
            }
         }

         /* Does (the final) wc exist in the applicable bind tree?
          * Does it (possibly) want more input? */
         tbtp = (isp != NIL) ? isp->tbtp->tbt_children
               : (*tlp->tl_bind_tree_hmap)[wc % a_TTY_PRIME];
         for(; tbtp != NIL; tbtp = tbtp->tbt_sibling){
            if(tbtp->tbt_char == wc){
               struct inseq *nisp;

               if(ttm != a_TTY_TTM_MBSEQ && tbtp->tbt_ismbseq)
                  continue;

               /* If this one cannot continue we are likely finished! */
               if(tbtp->tbt_children == NIL){
                  ASSERT(tbtp->tbt_bind != NIL);
                  tbf = tbtp->tbt_bind->tbc_flags;
                  goto jmle_fun;
               }

               /* Defer decision, try to read more characters */
               nisp = su_AUTO_ALLOC(sizeof *nisp);
               if((nisp->last = isp) == NIL)
                  isp_head = nisp;
               else
                  isp->next = nisp;
               nisp->next = NIL;
               nisp->tbtp = tbtp;
               isp = nisp;
               break;
            }
         }
         if(tbtp != NIL)
            goto jread_again;

         /* The special cancel bypass input function is inferior to key-
          * sequence continuation, so test it thereafter */
         if(su_cs_is_ascii(wc)){
            char const *cp;
            char c;

            for(c = S(char,wc), cp = &(*tlp->tl_bind_shcut_cancel)[0];
                  *cp != '\0'; ++cp)
               if(c == *cp){
                  tbf = a_TTY_BIND_FUN_INTERNAL | a_TTY_BIND_FUN_CANCEL;
                  goto jmle_fun;
               }
         }

         /* We know the wc read last has no binding.
          * If there was a partial binding active already, finalize it if
          * possible, otherwise take over all its characters.  And wc */
         if(isp != NIL){
            /* Can it be terminated? */
            if((tbtp = isp->tbtp)->tbt_bind != NIL){
jhave_bind:
               tbf = tbtp->tbt_bind->tbc_flags;
jmle_fun:
               if(tbf & a_TTY_BIND_FUN_INTERNAL){
                  switch(a_tty_fun(tlp, tbf, &len)){
                  case a_TTY_FUN_STATUS_OK:
                     goto jinput_loop;
                  case a_TTY_FUN_STATUS_COMMIT:
                     goto jdone;
                  case a_TTY_FUN_STATUS_RESTART:
                     goto jrestart;
                  case a_TTY_FUN_STATUS_END:
                     rv = -1;
                     goto jleave;
                  }
                  ASSERT(0);
               }else if(tbtp->tbt_bind->tbc_flags & a_TTY_BIND_NOCOMMIT){
                  struct a_tty_bind_ctx *tbcp;

                  tbcp = tbtp->tbt_bind;
                  su_mem_copy(tlp->tl_defc.s = su_AUTO_ALLOC(
                        (tlp->tl_defc.l = len = tbcp->tbc_exp_len) +1),
                     tbcp->tbc_exp, tbcp->tbc_exp_len +1);
                  goto jrestart;
               }else{
                  cbufp = tbtp->tbt_bind->tbc_exp;
                  goto jinject_input;
               }
            }
         }

         /* We need to take over all the sequence "as is" */
jtake_over:
         for(rv = 0, isp = isp_head; isp != NIL; ++rv, isp = isp->next)
            ;
         a_tty_check_grow(tlp, rv  su_DBG_LOC_ARGS_USE);
         for(isp = isp_head; isp != NIL; isp = isp->next)
            if(a_tty_kother(tlp, isp->tbtp->tbt_char)){
               /* TODO ??? */
            }

# else /* mx_HAVE_KEY_BINDINGS */
         /* if it is a control byte check whether it is a MLE
          * function.  Remarks: initially a complete duplicate to be able to
          * switch(), later converted to simply iterate over (an #ifdef'd
          * subset of) the MLE base_tuple table in order to have "a SPOF" */
         if(su_cs_is_ascii(wc) && su_cs_is_cntrl(S(unsigned char,wc))){
            struct a_tty_bind_builtin_tuple const *tbbtp, *tbbtp_max;
            char c;

            c = S(char,wc) ^ 0x40;
            tbbtp = a_tty_bind_base_tuples;
            tbbtp_max = &tbbtp[NELEM(a_tty_bind_base_tuples)];
jbuiltin_redo:
            for(; tbbtp < tbbtp_max; ++tbbtp){
               /* Assert default_tuple table is properly subset'ed */
               ASSERT(tbbtp->tbbt_iskey);
               if(tbbtp->tbbt_ckey == c){
                  if(tbbtp->tbbt_exp[0] == '\0'){
                     tbf = a_TTY_BIND_FUN_EXPAND(S(u8,tbbtp->tbbt_exp[1]));
                     switch(a_tty_fun(tlp, tbf, &len)){
                     case a_TTY_FUN_STATUS_OK:
                        goto jinput_loop;
                     case a_TTY_FUN_STATUS_COMMIT:
                        goto jdone;
                     case a_TTY_FUN_STATUS_RESTART:
                        goto jrestart;
                     case a_TTY_FUN_STATUS_END:
                        rv = -1;
                        goto jleave;
                     }
                     ASSERT(0);
                  }else{
                     cbufp = UNCONST(char*,tbbtp->tbbt_exp);
                     goto jinject_input;
                  }
               }
            }
            if(tbbtp == &a_tty_bind_base_tuples[
                  NELEM(a_tty_bind_base_tuples)]){
               tbbtp = a_tty_bind_default_tuples;
               tbbtp_max = &tbbtp[NELEM(a_tty_bind_default_tuples)];
               goto jbuiltin_redo;
            }
         }
#endif /* !mx_HAVE_KEY_BINDINGS */

         if(wc != '\0' && a_tty_kother(tlp, wc)){
# ifdef mx_HAVE_HISTORY
            if(cbuf == cbuf_base)
               tlp->tl_hist = NIL;
# endif
         }
         break;
      }
   }

jdone:
   /* We have a completed input line, convert the struct cell data to its
    * plain character equivalent */
   rv = a_tty_cell2dat(tlp);

   if(histok_or_nil != NIL &&
         (rv <= 0 || su_cs_is_space(tlp->tl_line.cbuf[0])))
      *histok_or_nil = FAL0;

jleave:
   putc('\n', mx_tty_fp);
   fflush(mx_tty_fp);
   NYD_OU;
   return rv;

jinject_input:{
   uz i;

   mx_sigs_all_holdx(); /* XXX v15 drop */
   i = a_tty_cell2dat(tlp);
   mx_go_input_inject(mx_GO_INPUT_INJECT_NONE, tlp->tl_line.cbuf, i);
   i = su_cs_len(cbufp) +1;
   if(i >= *tlp->tl_x_bufsize){
      *tlp->tl_x_buf = su_MEM_REALLOC_LOCOR(*tlp->tl_x_buf, i,
            su_DBG_LOC_ARGS_ORUSE);
      *tlp->tl_x_bufsize = i;
   }
   su_mem_copy(*tlp->tl_x_buf, cbufp, i);
   mx_sigs_all_rele(); /* XXX v15 drop */

   if(histok_or_nil != NIL)
      *histok_or_nil = FAL0;
   rv = S(sz,--i);
   }
   goto jleave;
}

# ifdef mx_HAVE_KEY_BINDINGS
static BITENUM_IS(u32,mx_go_input_flags)
a_tty_bind_ctx_find(char const *name){
   BITENUM_IS(u32,mx_go_input_flags) rv;
   struct a_tty_input_ctx_map const *ticmp;
   NYD2_IN;

   ticmp = a_tty_input_ctx_maps;
   do if(!su_cs_cmp_case(ticmp->ticm_name, name)){
      rv = ticmp->ticm_ctx;
      goto jleave;
   }while(PCMP(++ticmp, <,
      &a_tty_input_ctx_maps[NELEM(a_tty_input_ctx_maps)]));

   rv = R(BITENUM_IS(u32,mx_go_input_flags),-1);
jleave:
   NYD2_OU;
   return rv;
}

static boole
a_tty_bind_create(struct a_tty_bind_parse_ctx *tbpcp, boole replace){
   struct a_tty_bind_ctx *tbcp;
   boole rv;
   NYD2_IN;

   rv = FAL0;

   if(!a_tty_bind_parse(tbpcp, TRU1))
      goto jleave;

   /* Since we use a single buffer for it all, need to replace as such */
   if(tbpcp->tbpc_tbcp != NIL){
      if(!replace)
         goto jleave;
      a_tty_bind_del(tbpcp);
   }else if(a_tty.tg_bind_cnt == U32_MAX){
      n_err(_("bind: maximum number of bindings already established\n"));
      goto jleave;
   }

   /* C99 */{
      uz i, j;

      tbcp = su_ALLOC(VSTRUCT_SIZEOF(struct a_tty_bind_ctx,tbc__buf) +
            tbpcp->tbpc_seq_len +1 + tbpcp->tbpc_exp.l +1 +
            tbpcp->tbpc_cnv_align_mask + 1 + tbpcp->tbpc_cnv_len);
      if(tbpcp->tbpc_ltbcp != NIL){
         tbcp->tbc_next = tbpcp->tbpc_ltbcp->tbc_next;
         tbpcp->tbpc_ltbcp->tbc_next = tbcp;
      }else{
         BITENUM_IS(u32,mx_go_input_flags) gif;

         gif = tbpcp->tbpc_flags & mx__GO_INPUT_CTX_MASK;
         tbcp->tbc_next = a_tty.tg_bind[gif];
         a_tty.tg_bind[gif] = tbcp;
      }
      su_mem_copy(tbcp->tbc_seq = &tbcp->tbc__buf[0],
         tbpcp->tbpc_seq, i = (tbcp->tbc_seq_len = tbpcp->tbpc_seq_len) +1);
      ASSERT(tbpcp->tbpc_exp.l > 0);
      su_mem_copy(tbcp->tbc_exp = &tbcp->tbc__buf[i],
         tbpcp->tbpc_exp.s, j = (tbcp->tbc_exp_len = tbpcp->tbpc_exp.l) +1);
      i += j;
      i = (i + tbpcp->tbpc_cnv_align_mask) & ~tbpcp->tbpc_cnv_align_mask;
      su_mem_copy(tbcp->tbc_cnv = &tbcp->tbc__buf[i],
         tbpcp->tbpc_cnv, (tbcp->tbc_cnv_len = tbpcp->tbpc_cnv_len));
      tbcp->tbc_flags = tbpcp->tbpc_flags;
   }

   /* Directly resolve any termcap(5) symbol if we are already setup */
   if((n_psonce & n_PSO_STARTED_CONFIG) &&
         (tbcp->tbc_flags & (a_TTY_BIND_RESOLVE | a_TTY_BIND_DEFUNCT)) ==
          a_TTY_BIND_RESOLVE)
      a_tty_bind_resolve(tbcp);

   ++a_tty.tg_bind_cnt;
   /* If this binding is usable invalidate the key input lookup trees */
   if(!(tbcp->tbc_flags & a_TTY_BIND_DEFUNCT))
      a_tty.tg_bind_isdirty = TRU1;
   rv = TRU1;

jleave:
   NYD2_OU;
   return rv;
}

static boole
a_tty_bind_parse(struct a_tty_bind_parse_ctx *tbpcp, boole isbindcmd){
   enum{a_TRUE_RV = a_TTY__BIND_LAST<<1};

   struct mx_visual_info_ctx vic;
   struct str shin_save, shin;
   struct n_string shou, *shoup;
   uz i;
   struct kse{
      struct kse *next;
      char *seq_dat;
      wc_t *cnv_dat;
      u32 seq_len;
      u32 cnv_len;      /* High bit set if a termap to be resolved */
      u32 calc_cnv_len; /* Ditto, but aligned etc. */
      u8 kse__dummy[4];
   } *head, *tail;
   u32 f;
   NYD2_IN;
   LCTA(UCMP(64, a_TRUE_RV, <, U32_MAX),
      "Flag bits excess storage datatype");

   f = mx_GO_INPUT_NONE;
   shoup = n_string_creat_auto(&shou);
   head = tail = NIL;

   /* Parse the key-sequence */
   for(shin.s = UNCONST(char*,tbpcp->tbpc_in_seq), shin.l = UZ_MAX;;){
      struct kse *ep;
      BITENUM_IS(u32,n_shexp_state) shs;

      shin_save = shin;
      shs = n_shexp_parse_token((n_SHEXP_PARSE_TRUNC |
            n_SHEXP_PARSE_TRIM_SPACE | n_SHEXP_PARSE_IGNORE_EMPTY |
            n_SHEXP_PARSE_IFS_IS_COMMA), shoup, &shin, NIL);
      if(shs & n_SHEXP_STATE_ERR_UNICODE){
         f |= a_TTY_BIND_DEFUNCT;
         if(isbindcmd && (n_poption & n_PO_D_V))
            n_err(_("%s: \\uNICODE not available in locale: %s\n"),
               tbpcp->tbpc_cmd, tbpcp->tbpc_in_seq);
      }
      if((shs & n_SHEXP_STATE_ERR_MASK) & ~n_SHEXP_STATE_ERR_UNICODE){
         n_err(_("%s: failed to parse key-sequence: %s\n"),
            tbpcp->tbpc_cmd, tbpcp->tbpc_in_seq);
         goto jleave;
      }
      if((shs & (n_SHEXP_STATE_OUTPUT | n_SHEXP_STATE_STOP)) ==
            n_SHEXP_STATE_STOP)
         break;

      ep = su_AUTO_ALLOC(sizeof *ep);
      if(head == NIL)
         head = ep;
      else
         tail->next = ep;
      tail = ep;
      ep->next = NIL;
      if(!(shs & n_SHEXP_STATE_ERR_UNICODE)){
         i = su_cs_len(ep->seq_dat =
               n_shexp_quote_cp(n_string_cp(shoup), TRU1));
         if(i >= S32_MAX - 1)
            goto jelen;
         ep->seq_len = S(u32,i);
      }else{
         /* Otherwise use the original buffer, _we_ can only quote it the wrong
          * way (e.g., an initial $'\u3a' becomes '\u3a'), _then_ */
         if((i = shin_save.l - shin.l) >= S32_MAX - 1)
            goto jelen;
         ep->seq_len = S(u32,i);
         ep->seq_dat = savestrbuf(shin_save.s, i);
      }

      su_mem_set(&vic, 0, sizeof vic);
      vic.vic_inlen = shoup->s_len;
      vic.vic_indat = shoup->s_dat;
      if(!mx_visual_info(&vic,
            mx_VISUAL_INFO_WOUT_CREATE | mx_VISUAL_INFO_WOUT_AUTO_ALLOC)){
         n_err(_("%s: key-sequence seems to contain invalid "
            "characters: %s: %s\n"),
            tbpcp->tbpc_cmd, n_string_cp(shoup), tbpcp->tbpc_in_seq);
         f |= a_TTY_BIND_DEFUNCT;
         goto jleave;
      }else if(vic.vic_woulen == 0 ||
            vic.vic_woulen >= (S32_MAX - 2) / sizeof(wc_t)){
jelen:
         n_err(_("%s: length of key-sequence unsupported: %s: %s\n"),
            tbpcp->tbpc_cmd, n_string_cp(shoup), tbpcp->tbpc_in_seq);
         f |= a_TTY_BIND_DEFUNCT;
         goto jleave;
      }
      ep->cnv_dat = vic.vic_woudat;
      ep->cnv_len = S(u32,vic.vic_woulen);

      /* A termcap(5)/terminfo(5) identifier? */
      if(ep->cnv_len > 1 && ep->cnv_dat[0] == ':'){
         i = --ep->cnv_len, ++ep->cnv_dat;
#  if 0 /* ndef mx_HAVE_TERMCAP xxx User can, via *termcap*! */
         if(n_poption & n_PO_D_V)
            n_err(_("%s: no termcap(5)/terminfo(5) support: %s: %s\n"),
               tbpcp->tbpc_cmd, ep->seq_dat, tbpcp->tbpc_in_seq);
         f |= a_TTY_BIND_DEFUNCT;
#  endif
         if(i > a_TTY_BIND_CAPNAME_MAX){
            n_err(_("%s: termcap(5)/terminfo(5) name too long: %s: %s\n"),
               tbpcp->tbpc_cmd, ep->seq_dat, tbpcp->tbpc_in_seq);
            f |= a_TTY_BIND_DEFUNCT;
         }
         while(i > 0)
            /* (We store it as char[]) */
            if((u32)ep->cnv_dat[--i] & ~0x7Fu){
               n_err(_("%s: invalid termcap(5)/terminfo(5) name content: "
                  "%s: %s\n"),
                  tbpcp->tbpc_cmd, ep->seq_dat, tbpcp->tbpc_in_seq);
               f |= a_TTY_BIND_DEFUNCT;
               break;
            }
         ep->cnv_len |= S32_MIN; /* Needs resolve */
         f |= a_TTY_BIND_RESOLVE;
      }

      if(shs & n_SHEXP_STATE_STOP)
         break;
   }

   if(head == NIL){
jeempty:
      n_err(_("%s: effectively empty key-sequence: %s\n"),
         tbpcp->tbpc_cmd, tbpcp->tbpc_in_seq);
      goto jleave;
   }

   if(isbindcmd) /* (Or always, just "1st time init") */
      tbpcp->tbpc_cnv_align_mask = MAX(sizeof(s32), sizeof(wc_t)) - 1;

   /* C99 */{
      struct a_tty_bind_ctx *ltbcp, *tbcp;
      char *cpbase, *cp, *cnv;
      uz seql, cnvl;

      /* Unite the parsed sequence(s) into single string representations */
      for(seql = cnvl = 0, tail = head; tail != NIL; tail = tail->next){
         seql += tail->seq_len + 1;

         if(!isbindcmd)
            continue;

         /* Preserve room for terminal capabilities to be resolved.
          * Above we have ensured the buffer will fit in these calculations */
         if((i = tail->cnv_len) & S32_MIN){
            /* For now
             * struct{s32 buf_len_iscap; s32 cap_len; wc_t name[]+NUL;}
             * later
             * struct{s32 buf_len_iscap; s32 cap_len; char buf[]+NUL;} */
            LCTAV(IS_POW2(a_TTY_BIND_CAPEXP_ROUNDUP));
            LCTA(a_TTY_BIND_CAPEXP_ROUNDUP >= sizeof(wc_t),
               "Aligning on this constant does not properly align wc_t");
            i &= S32_MAX;
            i *= sizeof(wc_t);
            i += sizeof(s32);
            if(i < a_TTY_BIND_CAPEXP_ROUNDUP)
               i = (i + (a_TTY_BIND_CAPEXP_ROUNDUP - 1)) &
                     ~(a_TTY_BIND_CAPEXP_ROUNDUP - 1);
         }else
            /* struct{s32 buf_len_iscap; wc_t buf[]+NUL;} */
            i *= sizeof(wc_t);
         i += sizeof(s32) + sizeof(wc_t); /* (buf_len_iscap, NUL) */
         cnvl += i;
         if(tail->cnv_len & S32_MIN){
            tail->cnv_len &= S32_MAX;
            i |= S32_MIN;
         }
         tail->calc_cnv_len = (u32)i;
      }
      --seql;

      tbpcp->tbpc_seq_len = seql;
      tbpcp->tbpc_cnv_len = cnvl;
      /* C99 */{
         uz j;

         j = i = seql + 1; /* Room for comma separator */
         if(isbindcmd){
            i = (i + tbpcp->tbpc_cnv_align_mask) & ~tbpcp->tbpc_cnv_align_mask;
            j = i;
            i += cnvl;
         }
         tbpcp->tbpc_seq = cp = cpbase = su_AUTO_ALLOC(i);
         tbpcp->tbpc_cnv = cnv = &cpbase[j];
      }

      for(tail = head; tail != NIL; tail = tail->next){
         su_mem_copy(cp, tail->seq_dat, tail->seq_len);
         cp += tail->seq_len;
         *cp++ = ',';

         if(isbindcmd){
            char * const save_cnv = cnv;

            UNALIGN(s32*,cnv)[0] = S(s32,i = tail->calc_cnv_len);
            cnv += sizeof(s32);
            if(i & S32_MIN){
               /* For now
                * struct{s32 buf_len_iscap; s32 cap_len; wc_t name[];}
                * later
                * struct{s32 buf_len_iscap; s32 cap_len; char buf[];} */
               UNALIGN(s32*,cnv)[0] = tail->cnv_len;
               cnv += sizeof(s32);
            }
            i = tail->cnv_len * sizeof(wc_t);
            su_mem_copy(cnv, tail->cnv_dat, i);
            cnv += i;
            *UNALIGN(wc_t*,cnv) = '\0';

            cnv = save_cnv + (tail->calc_cnv_len & S32_MAX);
         }
      }
      *--cp = '\0';

      /* Search for a yet existing identical mapping */
      /* C99 */{
         BITENUM_IS(u32,mx_go_input_flags) gif;

         gif = tbpcp->tbpc_flags & mx__GO_INPUT_CTX_MASK;

         for(ltbcp = NIL, tbcp = a_tty.tg_bind[gif]; tbcp != NIL;
               ltbcp = tbcp, tbcp = tbcp->tbc_next)
            if(tbcp->tbc_seq_len == seql &&
                  !su_mem_cmp(tbcp->tbc_seq, cpbase, seql)){
               tbpcp->tbpc_tbcp = tbcp;
               break;
            }
      }
      tbpcp->tbpc_ltbcp = ltbcp;
      tbpcp->tbpc_flags |= (f & a_TTY__BIND_MASK);
   }

   /* Create single string expansion if so desired */
   if(isbindcmd){
      char *exp;

      if(isbindcmd == TRUM1){
         if(tbpcp->tbpc_tbcp != NIL)
            f |= a_TRUE_RV;
         goto jleave;
      }

      if((i = tbpcp->tbpc_exp.l) > 0){
         if((exp = tbpcp->tbpc_exp.s)[i - 1] == '@'){
#if 0 /* xxx no: allow trailing whitespace, as in 'echo du @' .. */
            while(--i > 0)
               if(!su_cs_is_space(exp[i - 1]))
                  break;
#else
            --i;
#endif
            if(i == 0)
               goto jeempty;

            exp[tbpcp->tbpc_exp.l = i] = '\0';
            tbpcp->tbpc_flags |= a_TTY_BIND_NOCOMMIT;
         }else{
            n_str_trim(&tbpcp->tbpc_exp, n_STR_TRIM_BOTH);
            i = tbpcp->tbpc_exp.l;
            exp = tbpcp->tbpc_exp.s;
         }
      }

      /* Reverse solidus cannot be placed last in expansion to avoid (at the
       * time of this writing) possible problems with newline escaping.
       * Don't care about (un)even number thereof */
      if(i > 0 && exp[i - 1] == '\\'){
         n_err(_("%s: reverse solidus cannot be last in expansion: %s\n"),
            tbpcp->tbpc_cmd, tbpcp->tbpc_in_seq);
         goto jleave;
      }

      /* TODO `bind': since empty expansions are forbidden it would be nice to
       * TODO be able to say "bind base a,b,c" and see the expansion of only
       * TODO that (just like we do for `alias', `commandalias' etc.!) */
      if(i == 0)
         goto jeempty;

      /* It may map to an internal MLE command! */
      for(i = 0; i < NELEM(a_tty_bind_fun_names); ++i)
         if(!su_cs_cmp_case(exp, a_tty_bind_fun_names[i])){
            tbpcp->tbpc_flags |= a_TTY_BIND_FUN_EXPAND(i) |
                  a_TTY_BIND_FUN_INTERNAL |
                  (head->next == NIL ? a_TTY_BIND_MLE1CNTRL : 0);
            if((n_poption & n_PO_D_V) &&
                  (tbpcp->tbpc_flags & a_TTY_BIND_NOCOMMIT))
               n_err(_("%s: MLE commands cannot be made editable via @: %s\n"),
                  tbpcp->tbpc_cmd, exp);
            tbpcp->tbpc_flags &= ~a_TTY_BIND_NOCOMMIT;
            break;
         }
   }

  f |= a_TRUE_RV; /* TODO because we only now true and false; DEFUNCT.. */
jleave:
   n_string_gut(shoup);

   NYD2_OU;
   return (f & a_TRUE_RV) != 0;
}

static void
a_tty_bind_resolve(struct a_tty_bind_ctx *tbcp){
   char capname[a_TTY_BIND_CAPNAME_MAX +1];
   struct mx_termcap_value tv;
   uz len;
   boole isfirst; /* TODO For now: first char must be control! TODO why? ;} */
   char *cp, *next;
   NYD2_IN;

   UNINIT(next, NIL);
   for(cp = tbcp->tbc_cnv, isfirst = TRU1, len = tbcp->tbc_cnv_len;
         len > 0; isfirst = FAL0, cp = next){
      /* C99 */{
         s32 i, j;

         i = UNALIGN(s32*,cp)[0];
         j = i & S32_MAX;
         next = &cp[j];
         len -= j;
         if(i == j)
            continue;

         /* struct{s32 buf_len_iscap; s32 cap_len; wc_t name[];} */
         cp += sizeof(s32);
         i = UNALIGN(s32*,cp)[0];
         cp += sizeof(s32);
         for(j = 0; j < i; ++j)
            capname[j] = UNALIGN(wc_t*,cp)[j];
         capname[j] = '\0';
      }

      /* Use generic lookup mechanism if not a known query */
      /* C99 */{
         s32 tq;

         tq = mx_termcap_query_for_name(capname, mx_TERMCAP_CAPTYPE_STRING);
         if(tq == -1){
            tv.tv_data.tvd_string = capname;
            tq = mx__TERMCAP_QUERY_MAX1;
         }

         if(tq < 0 || !mx_termcap_query(tq, &tv)){
            if(n_poption & n_PO_D_V)
               n_err(_("bind: unknown or unsupported capability: %s: %s\n"),
                  capname, tbcp->tbc_seq);
            tbcp->tbc_flags |= a_TTY_BIND_DEFUNCT;
            break;
         }
      }

      /* struct{s32 buf_len_iscap; s32 cap_len; char buf[]+NUL;} */
      /* C99 */{
         uz i;

         i = su_cs_len(tv.tv_data.tvd_string);
         if(/*i > S32_MAX ||*/ i >= P2UZ(next - cp)){
            if(n_poption & n_PO_D_V)
               n_err(_("bind: capability expansion too long: %s: %s\n"),
                  capname, tbcp->tbc_seq);
            tbcp->tbc_flags |= a_TTY_BIND_DEFUNCT;
            break;
         }else if(i == 0){
            if(n_poption & n_PO_D_V)
               n_err(_("bind: empty capability expansion: %s: %s\n"),
                  capname, tbcp->tbc_seq);
            tbcp->tbc_flags |= a_TTY_BIND_DEFUNCT;
            break;
         }else if(isfirst && !su_cs_is_cntrl(*tv.tv_data.tvd_string)){
            if(n_poption & n_PO_D_V)
               n_err(_("bind: capability expansion does not start with "
                  "control: %s: %s\n"), capname, tbcp->tbc_seq);
            tbcp->tbc_flags |= a_TTY_BIND_DEFUNCT;
            break;
         }
         UNALIGN(s32*,cp)[-1] = (s32)i;
         su_mem_copy(cp, tv.tv_data.tvd_string, i);
         cp[i] = '\0';
      }
   }

   NYD2_OU;
}

static u32
a_tty_bind_show(struct a_tty_bind_ctx *tbcp, FILE *fp){
   u32 rv, flags;
   NYD2_IN;

   rv = 0;

   /* Print the bytes of resolved terminal capabilities, then */
   if((n_poption & n_PO_D_V) &&
         (tbcp->tbc_flags & (a_TTY_BIND_RESOLVE | a_TTY_BIND_DEFUNCT)
          ) == a_TTY_BIND_RESOLVE){
      char cbuf[8], c;
      union {wchar_t const *wp; char const *cp;} u;
      s32 entlen;
      u32 cnvlen;
      char const *cnvdat, *bsep, *cbufp;

      putc('#', fp);
      putc(' ', fp);

      cbuf[0] = '=', cbuf[2] = '\0';
      for(cnvdat = tbcp->tbc_cnv, cnvlen = tbcp->tbc_cnv_len;
            cnvlen > 0;){
         if(cnvdat != tbcp->tbc_cnv)
            putc(',', fp);

         /* {s32 buf_len_iscap;} */
         entlen = *UNALIGN(s32 const*,cnvdat);
         if(entlen & S32_MIN){
            /* struct{s32 buf_len_iscap; s32 cap_len;
             * char buf[]+NUL;} */
            for(bsep = su_empty,
                     u.cp = S(char const*,
                           &UNALIGN(s32 const*,cnvdat)[2]);
                  (c = *u.cp) != '\0'; ++u.cp){
               if(su_cs_is_ascii(c) && !su_cs_is_cntrl(c))
                  cbuf[1] = c, cbufp = cbuf;
               else
                  cbufp = su_empty;
               fprintf(fp, "%s\\x%02X%s", bsep, S(u32,S(u8,c)), cbufp);
               bsep = " ";
            }
            entlen &= S32_MAX;
         }else
            putc('-', fp);

         cnvlen -= entlen;
         cnvdat += entlen;
      }

      fputs("\n  ", fp);
      rv = 1;
   }

   flags = tbcp->tbc_flags;
   fprintf(fp, "%sbind %s %s %s%s%s\n",
      ((flags & a_TTY_BIND_DEFUNCT)
      /* I18N: `bind' sequence not working, either because it is
       * I18N: using Unicode and that is not available in the locale,
       * I18N: or a termcap(5)/terminfo(5) sequence won't work out */
         ? _("# <Defunctional> ") : su_empty),
      a_tty_input_ctx_maps[flags & mx__GO_INPUT_CTX_MASK].ticm_name,
      tbcp->tbc_seq, n_shexp_quote_cp(tbcp->tbc_exp, TRU1),
      (flags & a_TTY_BIND_NOCOMMIT ? n_at : su_empty),
      (!(n_poption & n_PO_D_VV) ? su_empty
         : (flags & a_TTY_BIND_FUN_INTERNAL ? _(" # MLE internal") : su_empty))
      );

   NYD2_OU;
   return ++rv;
}

static void
a_tty_bind_del(struct a_tty_bind_parse_ctx *tbpcp){
   struct a_tty_bind_ctx *ltbcp, *tbcp;
   NYD2_IN;

   tbcp = tbpcp->tbpc_tbcp;

   if((ltbcp = tbpcp->tbpc_ltbcp) != NIL)
      ltbcp->tbc_next = tbcp->tbc_next;
   else
      a_tty.tg_bind[tbpcp->tbpc_flags & mx__GO_INPUT_CTX_MASK
            ] = tbcp->tbc_next;
   su_FREE(tbcp);

   --a_tty.tg_bind_cnt;
   a_tty.tg_bind_isdirty = TRU1;
   NYD2_OU;
}

static void
a_tty_bind_tree_build(void){
   uz i;
   NYD2_IN;

   for(i = 0; i < mx__GO_INPUT_CTX_MAX1; ++i){
      struct a_tty_bind_ctx *tbcp;
      LCTAV(mx_GO_INPUT_CTX_BASE == 0);

      /* Somewhat wasteful, but easier to handle: simply clone the entire
       * primary key onto the secondary one, then only modify it */
      for(tbcp = a_tty.tg_bind[mx_GO_INPUT_CTX_BASE]; tbcp != NIL;
            tbcp = tbcp->tbc_next)
         if(!(tbcp->tbc_flags & a_TTY_BIND_DEFUNCT))
            a_tty__bind_tree_add(i, &a_tty.tg_bind_tree[i][0], tbcp);

      if(i != mx_GO_INPUT_CTX_BASE)
         for(tbcp = a_tty.tg_bind[i]; tbcp != NIL; tbcp = tbcp->tbc_next)
            if(!(tbcp->tbc_flags & a_TTY_BIND_DEFUNCT))
               a_tty__bind_tree_add(i, &a_tty.tg_bind_tree[i][0], tbcp);
   }

   if(n_poption & n_PO_D_VVV){
      n_err(_("`bind': key binding tree:\n"));

      for(i = 0; i < mx__GO_INPUT_CTX_MAX1; ++i){
         uz j;

         n_err("  - %s:\n", a_tty_input_ctx_maps[i].ticm_name);

         for(j = 0; j < a_TTY_PRIME; ++j)
            a_tty__bind_tree_dump(a_tty.tg_bind_tree[i][j], "    ");
      }
   }

   a_tty.tg_bind_isbuild = TRU1;
   NYD2_OU;
}

static void
a_tty_bind_tree_teardown(void){
   uz i, j;
   NYD2_IN;

   su_mem_set(&a_tty.tg_bind_shcut_cancel[0], 0,
      sizeof(a_tty.tg_bind_shcut_cancel));
   su_mem_set(&a_tty.tg_bind_shcut_prompt_char[0], 0,
      sizeof(a_tty.tg_bind_shcut_prompt_char));

   for(i = 0; i < mx__GO_INPUT_CTX_MAX1; ++i)
      for(j = 0; j < a_TTY_PRIME; ++j)
         a_tty__bind_tree_free(a_tty.tg_bind_tree[i][j]);
   su_mem_set(&a_tty.tg_bind_tree[0], 0, sizeof(a_tty.tg_bind_tree));

   a_tty.tg_bind_isdirty = a_tty.tg_bind_isbuild = FAL0;
   NYD2_OU;
}

static void
a_tty__bind_tree_add(u32 hmap_idx,
      struct a_tty_bind_tree *store[a_TTY_PRIME], struct a_tty_bind_ctx *tbcp){
   u32 cnvlen;
   char const *cnvdat;
   struct a_tty_bind_tree *ntbtp;
   NYD2_IN;

   ntbtp = NIL;

   for(cnvdat = tbcp->tbc_cnv, cnvlen = tbcp->tbc_cnv_len; cnvlen > 0;){
      union {wchar_t const *wp; char const *cp;} u;
      boole ismbseq_x, ismbseq;
      s32 entlen;

      /* {s32 buf_len_iscap;} */
      entlen = *UNALIGN(s32 const*,cnvdat);

      if(entlen & S32_MIN){
         /* struct{s32 buf_len_iscap; s32 cap_len; char buf[]+NUL;}
          * Note that empty capabilities result in DEFUNCT */
         for(ismbseq = FAL0,
                  u.cp = S(char const*,&UNALIGN(s32 const*,cnvdat)[2]);
               *u.cp != '\0'; ismbseq = TRU1, ++u.cp)
            ntbtp = a_tty__bind_tree_add_wc(store, ntbtp, *u.cp, ismbseq);
         ASSERT(ntbtp != NIL);
         entlen &= S32_MAX;
      }else{
         /* struct{s32 buf_len_iscap; wc_t buf[]+NUL;} */
         u.wp = S(wchar_t const*,&UNALIGN(s32 const*,cnvdat)[1]);

         /* May be a special shortcut function?
          * Since bind_tree_build() is easy and clones over CTX_BASE to the
          * "real" contexts: do not put the same shortcut several times */
         if(ntbtp == NIL && (tbcp->tbc_flags & a_TTY_BIND_MLE1CNTRL)){
            char *cp;
            u32 fun;

            fun = tbcp->tbc_flags & a_TTY__BIND_FUN_MASK;

            if(fun == a_TTY_BIND_FUN_CANCEL){
               for(cp = &a_tty.tg_bind_shcut_cancel[hmap_idx][0];
                     PCMP(cp, <, &a_tty.tg_bind_shcut_cancel[hmap_idx][
                        NELEM(a_tty.tg_bind_shcut_cancel[hmap_idx]) - 1]);
                     ++cp)
                  if(*cp == '\0'){
                     *cp = S(char,*u.wp);
                     break;
                  }else if(*cp == S(char,*u.wp))
                     break;
            }else if(fun == a_TTY_BIND_FUN_PROMPT_CHAR){
               for(cp = &a_tty.tg_bind_shcut_prompt_char[hmap_idx][0];
                     PCMP(cp, <, &a_tty.tg_bind_shcut_prompt_char[hmap_idx][
                        NELEM(a_tty.tg_bind_shcut_prompt_char[hmap_idx]) - 1]);
                     ++cp)
                  if(*cp == '\0'){
                     *cp = S(char,*u.wp);
                     break;
                  }else if(*cp == S(char,*u.wp))
                     break;
            }
         }

         ismbseq_x = (u.wp[1] != '\0');
         for(ismbseq = FAL0; *u.wp != '\0'; ismbseq = ismbseq_x, ++u.wp)
            ntbtp = a_tty__bind_tree_add_wc(store, ntbtp, *u.wp, ismbseq);
      }

      cnvlen -= entlen;
      cnvdat += entlen;
   }

   /* Should have been rendered defunctional at first instead */
   ASSERT(ntbtp != NIL);
   ntbtp->tbt_bind = tbcp;

   NYD2_OU;
}

static struct a_tty_bind_tree *
a_tty__bind_tree_add_wc(struct a_tty_bind_tree **treep,
      struct a_tty_bind_tree *parentp, wchar_t wc, boole ismbseq){
   /* XXX bind_tree_add_wc: yet appends; alphasort? balanced tree? */
   boole any;
   struct a_tty_bind_tree *tbtp, *xtbtp;
   NYD2_IN;

   if(parentp == NIL)
      treep += wc % a_TTY_PRIME;
   else
      treep = &parentp->tbt_children;

   /* Having no parent also means that the tree slot is possibly empty */
   for(any = FAL0, xtbtp = NIL, tbtp = *treep; tbtp != NIL;
         xtbtp = tbtp, tbtp = tbtp->tbt_sibling){
      if(tbtp->tbt_char != wc){
         if(any)
            break;
         continue;
      }
      any = TRU1;

      if(tbtp->tbt_ismbseq == ismbseq)
         goto jleave;

      /* ismbseq MUST be linked before !ismbseq, so record this "parent"
       * sibling, but continue searching for now.
       * Otherwise it is impossible that we will find what we look for */
      if(ismbseq){
#if ASSERT_INJOR(1, 0)
         while((tbtp = tbtp->tbt_sibling) != NIL)
            ASSERT(tbtp->tbt_char != wc);
#endif
         break;
      }
   }

   tbtp = su_CALLOC(sizeof *tbtp);
   tbtp->tbt_parent = parentp;
   tbtp->tbt_char = wc;
   tbtp->tbt_ismbseq = ismbseq;

   if(xtbtp == NIL){
      tbtp->tbt_sibling = *treep;
      *treep = tbtp;
   }else{
      tbtp->tbt_sibling = xtbtp->tbt_sibling;
      xtbtp->tbt_sibling = tbtp;
   }

jleave:
   NYD2_OU;
   return tbtp;
}

static void
a_tty__bind_tree_dump(struct a_tty_bind_tree const *tbtp, char const *indent){
   NYD2_IN;

   for(; tbtp != NIL; tbtp = tbtp->tbt_sibling){
      /* C99 */{
         char const *s1, *s2, *s3;

         if(tbtp->tbt_bind == NIL)
            s1 = s2 = s3 = su_empty;
         else
            s1 = "(", s2 = tbtp->tbt_bind->tbc_exp, s3 = ")";
         n_err("%s%c 0x%04X/%c %s%s%s\n", indent,
            (tbtp->tbt_ismbseq ? ':' : '.'), tbtp->tbt_char,
            (su_cs_is_print(tbtp->tbt_char) ? S(char,tbtp->tbt_char) : ' '),
            s1, s2, s3);
      }

      if(tbtp->tbt_children != NIL)
         a_tty__bind_tree_dump(tbtp->tbt_children, savecat(indent, "  "));
   }

   NYD2_OU;
}

static void
a_tty__bind_tree_free(struct a_tty_bind_tree *tbtp){
   NYD2_IN;

   while(tbtp != NIL){
      struct a_tty_bind_tree *tmp;

      if((tmp = tbtp->tbt_children) != NIL)
         a_tty__bind_tree_free(tmp);

      tmp = tbtp->tbt_sibling;
      su_FREE(tbtp);
      tbtp = tmp;
   }

   NYD2_OU;
}
# endif /* mx_HAVE_KEY_BINDINGS */

void
mx_tty_init(void){
   NYD_IN;

   if(ok_blook(line_editor_disable))
      goto jleave;

   /* Load the history file */
# ifdef mx_HAVE_HISTORY
   a_tty_hist_load();
# endif

   /* Force immediate resolve for anything which follows */
   n_psonce |= n_PSO_LINE_EDITOR_INIT;

# ifdef mx_HAVE_KEY_BINDINGS
   /* `bind's (and `unbind's) done from within resource files couldn't be
    * performed for real since our termcap driver wasn't yet loaded, and we
    * can't perform automatic init since the user may have disallowed so */
   /* C99 */{ /* TODO outsource into own file */
      struct a_tty_bind_ctx *tbcp;
      BITENUM_IS(u32,mx_go_input_flags) gif;

      for(gif = 0; gif < mx__GO_INPUT_CTX_MAX1; ++gif)
         for(tbcp = a_tty.tg_bind[gif]; tbcp != NIL; tbcp = tbcp->tbc_next)
            if((tbcp->tbc_flags & (a_TTY_BIND_RESOLVE | a_TTY_BIND_DEFUNCT)
                  ) == a_TTY_BIND_RESOLVE)
               a_tty_bind_resolve(tbcp);
   }

   /* And we want to (try to) install some default key bindings */
   if(!ok_blook(line_editor_no_defaults)){
      char buf[8];
      struct a_tty_bind_parse_ctx tbpc;
      struct a_tty_bind_builtin_tuple const *tbbtp, *tbbtp_max;
      u32 flags;

      buf[0] = '$', buf[1] = '\'', buf[2] = '\\', buf[3] = 'c',
         buf[5] = '\'', buf[6] = '\0';

      tbbtp = a_tty_bind_base_tuples;
      tbbtp_max = &tbbtp[NELEM(a_tty_bind_base_tuples)];
      flags = mx_GO_INPUT_CTX_BASE;
jbuiltin_redo:
      for(; tbbtp < tbbtp_max; ++tbbtp){
         su_mem_set(&tbpc, 0, sizeof tbpc);
         tbpc.tbpc_cmd = "bind";
         if(tbbtp->tbbt_iskey){
            buf[4] = tbbtp->tbbt_ckey;
            tbpc.tbpc_in_seq = buf;
         }else
            tbpc.tbpc_in_seq = savecatsep(":", '\0',
               mx_termcap_name_of_query(tbbtp->tbbt_query));
         tbpc.tbpc_exp.s = UNCONST(char*,tbbtp->tbbt_exp[0] == '\0'
               ? a_tty_bind_fun_names[S(u8,tbbtp->tbbt_exp[1])]
               : tbbtp->tbbt_exp);
         tbpc.tbpc_exp.l = su_cs_len(tbpc.tbpc_exp.s);
         tbpc.tbpc_flags = flags;
         /* ..but don't want to overwrite any user settings */
         a_tty_bind_create(&tbpc, FAL0);
      }
      if(flags == mx_GO_INPUT_CTX_BASE){
         tbbtp = a_tty_bind_default_tuples;
         tbbtp_max = &tbbtp[NELEM(a_tty_bind_default_tuples)];
         flags = mx_GO_INPUT_CTX_DEFAULT;
         goto jbuiltin_redo;
      }
   }
# endif /* mx_HAVE_KEY_BINDINGS */

jleave:
   NYD_OU;
}

void
mx_tty_destroy(boole xit_fastpath){
   NYD_IN;

   if(!(n_psonce & n_PSO_LINE_EDITOR_INIT))
      goto jleave;

   /* Write the history file */
# ifdef mx_HAVE_HISTORY
   if(!xit_fastpath)
      a_tty_hist_save();
# endif

# if defined mx_HAVE_KEY_BINDINGS && defined mx_HAVE_DEBUG
   if(!xit_fastpath)
      mx_go_command(mx_GO_INPUT_NONE, "unbind * *");
# endif

# ifdef mx_HAVE_DEBUG
   su_mem_set(&a_tty, 0, sizeof a_tty);

   n_psonce &= ~n_PSO_LINE_EDITOR_INIT;
# endif

jleave:
   NYD_OU;
}

int
(mx_tty_readline)(BITENUM_IS(u32,mx_go_input_flags) gif, char const *prompt,
      char **linebuf, uz *linesize, uz n, boole *histok_or_nil
      su_DBG_LOC_ARGS_DECL){
   struct a_tty_line tl;
   struct n_string xprompt;
   sz nn;
   NYD_IN;

   ASSERT(!ok_blook(line_editor_disable));
   if(!(n_psonce & n_PSO_LINE_EDITOR_INIT))
      mx_tty_init();
   ASSERT(n_psonce & n_PSO_LINE_EDITOR_INIT);

   su_mem_set(&tl, 0, sizeof tl);
   tl.tl_goinflags = gif;
   if(!(gif & mx_GO_INPUT_PROMPT_NONE)){
      tl.tl_prompt = n_string_creat_auto(&xprompt);
      tl.tl_prompt_base = prompt;
   }
   a_tty_line_config(&tl, TRU1);

   tl.tl_line.cbuf = *linebuf;
   if(n != 0){
      tl.tl_defc.s = savestrbuf(*linebuf, n);
      tl.tl_defc.l = n;
   }
   tl.tl_x_buf = linebuf;
   tl.tl_x_bufsize = linesize;

   a_tty.tg_line = &tl;
   mx_termios_cmd(mx_TERMIOS_CMD_PUSH | mx_TERMIOS_CMD_RAW, 1);
   mx_termios_on_state_change_set(&a_tty_on_state_change, S(up,NIL));
   mx_TERMCAP_RESUME(FAL0);
   nn = a_tty_readline(&tl, n, histok_or_nil  su_DBG_LOC_ARGS_USE);
   /*mx_COLOUR( mx_colour_env_gut(); )
    *mx_TERMCAP_SUSPEND(FAL0);*/
   mx_termios_cmdx(mx_TERMIOS_CMD_POP | mx_TERMIOS_CMD_RAW);
   a_tty.tg_line = NIL;

   NYD_OU;
   return S(int,nn);
}

void
mx_tty_addhist(char const *s, BITENUM_IS(u32,mx_go_input_flags) gif){
   NYD_IN;
   UNUSED(s);
   UNUSED(gif);

   ASSERT(!(gif & mx_GO_INPUT_HIST_ERROR) || (gif & mx_GO_INPUT_HIST_GABBY));

# ifdef mx_HAVE_HISTORY
   if(*s != '\0' && (n_psonce & n_PSO_LINE_EDITOR_INIT) &&
         a_tty.tg_hist_size_max > 0 &&
         (!(gif & mx_GO_INPUT_HIST_GABBY) || a_tty_hist_is_gabby_ok(gif)) &&
          !ok_blook(line_editor_disable)){
      struct a_tty_input_ctx_map const *ticmp;

      ticmp = &a_tty_input_ctx_maps[gif & a_TTY_HIST_CTX_MASK];

      /* TODO *on-history-addition*: a future version will give the expanded
       * TODO command name as the third argument, followed by the tokenized
       * TODO command line as parsed in the remaining arguments, the first of
       * TODO which is the original unexpanded command name; i.e., one may do
       * TODO "shift 4" and access the arguments normal via $#, $@ etc. */
      if(temporary_addhist_hook(ticmp->ticm_name,
            ((gif & mx_GO_INPUT_HIST_GABBY)
               ? ((gif & mx_GO_INPUT_HIST_ERROR) ? "errors" : "all")
               : su_empty), s)){
         mx_sigs_all_holdx();
         (void)a_tty_hist_add(s, gif);
         mx_sigs_all_rele();
      }
   }
# endif /* mx_HAVE_HISTORY */

   NYD_OU;
}

# ifdef mx_HAVE_HISTORY
int
c_history(void *vp){
   char const **argv;
   boole x;
   NYD_IN;

   if(ok_blook(line_editor_disable)){
      n_err(_("history: *line-editor-disable* is set\n"));
      x = FAL0;
      goto jleave;
   }

   if(!(n_psonce & n_PSO_LINE_EDITOR_INIT)){
      mx_tty_init();
      ASSERT(n_psonce & n_PSO_LINE_EDITOR_INIT);
   }

   x = TRUM1;
   if(*(argv = vp) == NIL)
      x = a_tty_hist_list();
   else if(su_cs_starts_with_case("delete", *argv))
      x = a_tty_hist_sel_or_del(++argv, TRU1);
   else if(argv[1] == NIL){
      if(su_cs_starts_with_case("clear", *argv))
         x = a_tty_hist_clear();
      else if(su_cs_starts_with_case("load", *argv))
         x = a_tty_hist_load();
      else if(su_cs_starts_with_case("save", *argv))
         x = a_tty_hist_save();
      else if(su_cs_starts_with_case("show", *argv))
         x = a_tty_hist_list();
      else
         x = a_tty_hist_sel_or_del(argv, FAL0);
   }

   if(x < FAL0)
      mx_cmd_print_synopsis(mx_cmd_by_name_firstfit("history"), NIL);

jleave:
   NYD_OU;
   return (x > FAL0 ? n_EXIT_OK : n_EXIT_ERR);
}
# endif /* mx_HAVE_HISTORY */

# ifdef mx_HAVE_KEY_BINDINGS
int
c_bind(void *vp){
   struct a_tty_bind_ctx *tbcp;
   union {char const *cp; char *p; char c;} c;
   boole show, aster;
   BITENUM_IS(u32,mx_go_input_flags) gif;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   cacp = vp;
   gif = 0;

   if(cacp->cac_no == 0)
      aster = show = TRU1;
   else{
      c.cp = cacp->cac_arg->ca_arg.ca_str.s;
      show = (cacp->cac_no == 1);
      aster = FAL0;

      if((gif = a_tty_bind_ctx_find(c.cp)
            ) == R(BITENUM_IS(u32,mx_go_input_flags),-1)){
         if(!(aster = n_is_all_or_aster(c.cp)) || !show){
            n_err(_("bind: invalid context: %s\n"), c.cp);
            vp = NIL;
            goto jleave;
         }
         gif = 0;
      }
   }

   if(show){
      u32 lns;
      FILE *fp;

      if((fp = mx_fs_tmp_open(NIL, "bind", (mx_FS_O_RDWR | mx_FS_O_UNLINK),
               NIL)) == NIL)
         fp = n_stdout;

      lns = 0;
      for(;;){
         for(tbcp = a_tty.tg_bind[gif]; tbcp != NIL; tbcp = tbcp->tbc_next)
            lns += a_tty_bind_show(tbcp, fp);
         if(!aster || ++gif >= mx__GO_INPUT_CTX_MAX1)
            break;
      }

      if(fp != n_stdout){
         page_or_print(fp, lns);

         mx_fs_close(fp);
      }else
         clearerr(fp);
   }else{
      struct a_tty_bind_parse_ctx tbpc;
      struct mx_cmd_arg *cap;

      su_mem_set(&tbpc, 0, sizeof tbpc);
      tbpc.tbpc_flags = gif;
      tbpc.tbpc_cmd = cacp->cac_desc->cad_name;
      tbpc.tbpc_in_seq = (cap = cacp->cac_arg->ca_next)->ca_arg.ca_str.s;

      if((cap = cap->ca_next) != NIL){
         tbpc.tbpc_exp.s = cap->ca_arg.ca_str.s;
         tbpc.tbpc_exp.l = cap->ca_arg.ca_str.l;

         if(!a_tty_bind_create(&tbpc, TRU1))
            vp = NIL;
      }else{
         if(!a_tty_bind_parse(&tbpc, TRUM1) || tbpc.tbpc_tbcp == NIL){
            ASSERT(gif != R(BITENUM_IS(u32,mx_go_input_flags),-1));
            n_err(_("bind: no such `bind'ing to show: %s %s\n"),
               a_tty_input_ctx_maps[gif].ticm_name,
               n_shexp_quote_cp(tbpc.tbpc_in_seq, FAL0));
            vp = NIL;
         }else{
            a_tty_bind_show(tbpc.tbpc_tbcp, n_stdout);
            clearerr(n_stdout);
         }
      }
   }

jleave:
   NYD_OU;
   return (vp != NIL) ? n_EXIT_OK : n_EXIT_ERR;
}

int
c_unbind(void *vp){
   struct a_tty_bind_parse_ctx tbpc;
   struct a_tty_bind_ctx *tbcp;
   BITENUM_IS(u32,mx_go_input_flags) gif;
   boole aster;
   union {char const *cp; char *p;} c;
   struct mx_cmd_arg_ctx *cacp;
   NYD_IN;

   cacp = vp;
   c.cp = cacp->cac_arg->ca_arg.ca_str.s;
   aster = FAL0;

   if((gif = a_tty_bind_ctx_find(c.cp)
         ) == R(BITENUM_IS(u32,mx_go_input_flags),-1)){
      if(!(aster = n_is_all_or_aster(c.cp))){
         n_err(_("unbind: invalid context: %s\n"), c.cp);
         vp = NIL;
         goto jleave;
      }
      gif = mx_GO_INPUT_NONE;
   }

   c.cp = cacp->cac_arg->ca_next->ca_arg.ca_str.s;
jredo:
   if(n_is_all_or_aster(c.cp)){
      while((tbcp = a_tty.tg_bind[gif]) != NIL){
         su_mem_set(&tbpc, 0, sizeof tbpc);
         tbpc.tbpc_tbcp = tbcp;
         tbpc.tbpc_flags = gif;
         a_tty_bind_del(&tbpc);
      }
   }else{
      su_mem_set(&tbpc, 0, sizeof tbpc);
      tbpc.tbpc_cmd = cacp->cac_desc->cad_name;
      tbpc.tbpc_in_seq = c.cp;
      tbpc.tbpc_flags = gif;

      if(UNLIKELY(!a_tty_bind_parse(&tbpc, FAL0)))
         vp = NIL;
      else if(UNLIKELY((tbcp = tbpc.tbpc_tbcp) == NIL)){
         n_err(_("unbind: no such `bind'ing: %s  %s\n"),
            a_tty_input_ctx_maps[gif].ticm_name, c.cp);
         vp = NIL;
      }else
         a_tty_bind_del(&tbpc);
   }

   if(aster && ++gif < mx__GO_INPUT_CTX_MAX1)
      goto jredo;

jleave:
   NYD_OU;
   return (vp != NIL) ? n_EXIT_OK : n_EXIT_ERR;
}
# endif /* mx_HAVE_KEY_BINDINGS */

#else /* mx_HAVE_MLE */
/*
 * The really-nothing-at-all implementation
 */

# if 0
void
mx_tty_init(void){
   NYD_IN;
   NYD_OU;
}

void
mx_tty_destroy(boole xit_fastpath){
   NYD_IN;
   UNUSED(xit_fastpath);
   NYD_OU;
}
# endif /* 0 */

int
(mx_tty_readline)(BITENUM_IS(u32,mx_go_input_flags) gif, char const *prompt,
      char **linebuf, uz *linesize, uz n, boole *histok_or_nil
      su_DBG_LOC_ARGS_DECL){
   struct n_string xprompt;
   int rv;
   NYD_IN;
   UNUSED(histok_or_nil);

   if(!(gif & mx_GO_INPUT_PROMPT_NONE)){
      if(mx_tty_create_prompt(n_string_creat_auto(&xprompt), prompt, gif) > 0){
         fwrite(xprompt.s_dat, 1, xprompt.s_len, mx_tty_fp);
         fflush(mx_tty_fp);
      }
   }

   rv = (readline_restart)(n_stdin, linebuf, linesize, n  su_DBG_LOC_ARGS_USE);

   NYD_OU;
   return rv;
}

void
mx_tty_addhist(char const *s, BITENUM_IS(u32,mx_go_input_flags) gif){
   NYD_IN;
   UNUSED(s);
   UNUSED(gif);
   NYD_OU;
}
#endif /* nothing at all */

#include "su/code-ou.h"
#undef su_FILE
#undef mx_SOURCE
#undef mx_SOURCE_TTY_MLE
/* s-it-mode */
