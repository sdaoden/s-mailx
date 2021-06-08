/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ `digmsg'.
 *
 * Copyright (c) 2016 - 2021 Steffen (Daode) Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_DIG_MSG_H
#define mx_DIG_MSG_H

#include <mx/nail.h>

#include <su/mem-bag.h>

#define mx_HEADER
#include <su/code-in.h>

struct mx_dig_msg_ctx;

enum mx_dig_msg_flags{
   mx_DIG_MSG_NONE,
   mx_DIG_MSG_COMPOSE = 1u<<0, /* Compose mode object.. */
   mx_DIG_MSG_COMPOSE_DIGGED = 1u<<1, /* ..with `digmsg' handle also! */
   mx_DIG_MSG_RDONLY = 1u<<2, /* Message is read-only */
   mx_DIG_MSG_OWN_MEMBAG = 1u<<3, /* .gdm_membag==&.gdm__membag_buf[0] */
   mx_DIG_MSG_HAVE_FP = 1u<<4, /* Open on a fs_tmp_open() file */
   mx_DIG_MSG_FCLOSE = 1u<<5 /* (mx_HAVE_FP:) needs fclose() */
};

struct mx_dig_msg_ctx{
   struct mx_dig_msg_ctx *dmc_last; /* Linked only if !DIG_MSG_COMPOSE */
   struct mx_dig_msg_ctx *dmc_next;
   struct message *dmc_mp; /* XXX NIL if DIG_MSG_COMPOSE */
   struct mimepart *dmc_mime; /* XXX ditto */
   BITENUM_IS(u32,mx_dig_msg_flags) dmc_flags;
   u32 dmc_msgno; /* XXX Only if !DIG_MSG_COMPOSE */
   FILE *dmc_fp;
   struct header *dmc_hp;
   struct su_mem_bag *dmc_membag;
   struct su_mem_bag dmc__membag_buf[1];
};

/* Compose mode uses a "pseudo object" <> mx_dig_msg_compose_ctx TODO
 * A bit hairy (requires mx/go.h + su/mem.h that are NOT included) */
#define mx_DIG_MSG_COMPOSE_CREATE(DMCP,HP) \
do{\
   mx_dig_msg_compose_ctx = DMCP;\
   su_mem_set(mx_dig_msg_compose_ctx, 0, sizeof(*mx_dig_msg_compose_ctx));\
   (DMCP)->dmc_flags = mx_DIG_MSG_COMPOSE;\
   (DMCP)->dmc_hp = HP;\
   (DMCP)->dmc_membag = su_mem_bag_top(mx_go_data->gdc_membag);\
}while(0)

#define mx_DIG_MSG_COMPOSE_GUT(DMCP) \
do{\
   ASSERT(mx_dig_msg_compose_ctx == DMCP);\
   /* File is cleaned up via fs_close_all_files() */\
   mx_dig_msg_compose_ctx = NIL;\
}while(0)

EXPORT_DATA struct mx_dig_msg_ctx *mx_dig_msg_compose_ctx;
/* TODO The mx_dig_msg_read_overlay is one more hack to work around a missing
 * TODO OnLineCompleteEvent mechanism, to allow `read' within macros etc. */
EXPORT_DATA struct mx_dig_msg_ctx *mx_dig_msg_read_overlay;

/* XXX mx_dig_msg_on_mailbox_close() is a hack; it should be a handler of
 * XXX a regular OnMailboxClose event emitted by boxes */
EXPORT void mx_dig_msg_on_mailbox_close(struct mailbox *mbox);

/* `digmsg' */
EXPORT int c_digmsg(void *vp);

/* Accessibility hook for `~^' command; needs mx_DIG_MSG_COMPOSE_CREATE() */
EXPORT boole mx_dig_msg_circumflex(struct mx_dig_msg_ctx *dmcp, FILE *fp,
      char const *cmd);

#include <su/code-ou.h>
#endif /* mx_DIG_MSG_H */
/* s-it-mode */
