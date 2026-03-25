/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ enum okeys, and support.  Implementation in accmacvar.c.
 *
 * Copyright (c) 2012 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>.
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
#ifndef mx_OKEYS_H
#define mx_OKEYS_H

#include <mx/nail.h>

#define mx_HEADER
#include <su/code-in.h>

/* {{{ okeys
 * A large enum with all the boolean and value options aka their keys.
 * Only the constant keys are in here, to be looked up via ok_[bv]look(),
 * ok_[bv]set() and ok_[bv]clear().
 *
 * HOWTO add a new non-dynamic boolean or value option:
 * - add an entry to this enum (icase-sorted!)
 * - run make-okey-map.pl
 * - update manual!
 *
 * Properties understood by make-okey-map.pl; + entries have _VF_ flag bit.
 * They are placed in {PROP=VALUE[:,PROP=VALUE:]} comments, a {\} comment
 * causes the next line to be read for (overlong) properties.
 * - name=VAL when _->- conversion not applicable or name not alnum: real name
 * + chain=1 var has -HOST and/or -USER@HOST variants
 * + virt=1 synthesized var, implies rdonly= and nodel=
 * + vip=1 has special code checks in accmacvar.c
 * + rdonly=1 read-only (maybe: except in PS_ROOT_BLOCK())
 * + nodel=1 may not be unset
 * + i3val=VAL initial setting used once, unless var explicitly set (mutual defval=)
 * + defval=VAL if unset VAL is used (mutual i3val=)
 * + import=1 import ONLY from environment (pre PS_STARTED), implies env=
 * + env=1 update environment on change
 * + nolopts=1 no `local' etc tracking
 * + notempty=1 value must not be empty
 * + num=1 value must be 32-bit number (mutual posnum=)
 * + posnum=1 value must be positive 32-bit number (mutual num=)
 * + lower=1 value will be ASCII-lowercased
 * + obsolete=1 var is obsolete
 * - rc=1/VAL|norc=1: i3val=/defval= is "builtin rc" "UI improvement";
 *   If VAL, it is original setting; update manual!?  Adjust $OKEY_RC_ in test!!
 * - posix=1 means variable is part of POSIX standard
 *
 * Notes:
 * - Most default VAL_ues come from in from build system via ./make.rc
 * - Other i3val=s and/or defval=s are imposed by POSIX
 * - Keep in sync with manual and eg t_posix__compat() test
 * - See the introductional source comments before changing *anything* in here
 * TODO: could auto-generate posix tests etc */
enum okeys{
	/* This is used for all macro(-local) variables etc., i.e.,
	 * [*@#]|[1-9][0-9]*, in order to have something with correct properties.
	 * It is also used for the ${^.+} multiplexer */
	ok_v___special_param, /* {nolopts=1,rdonly=1,nodel=1} */
	/*__qm/__em aka ?/! should be num=1 but that more expensive than what now */
	ok_v___qm, /* {name=?,nolopts=1,rdonly=1,nodel=1} */
	ok_v___em, /* {name=!,nolopts=1,rdonly=1,nodel=1} */

	ok_v_account, /* {nolopts=1,rdonly=1,nodel=1} */
ok_v_agent_shell_lookup, /* {obsolete=1} */
	ok_b_allnet, /* {posix=1} */
	/* *ask* is auto-mapped to *asksub* as imposed by standard! */
	ok_b_ask, /* {vip=1} */
	ok_b_askatend,
	ok_b_askattach,
	ok_b_askbcc, /* {posix=1} */
	ok_b_askcc, /* {posix=1} */
	ok_b_asksign,
	ok_b_asksend, /* {i3val=TRU1,norc=1} */
	ok_b_asksub, /* {posix=1,i3val=TRU1} */
	ok_v_attrlist,
	ok_v_autobcc,
	ok_v_autocc,
	ok_b_autocollapse,
	ok_b_autoprint, /* {posix=1} */
ok_b_autothread, /* {obsolete=1} */
	ok_v_autosort,

	ok_b_bang, /* {posix=1} */
	ok_v_bang_data, /* {nolopts=1,rdonly=1,nodel=1} */
ok_v_bind_timeout, /* {vip=1,obsolete=1,notempty=1,posnum=1} */
	ok_v_bind_inter_byte_timeout, /* {\ } */
		/* {notempty=1,posnum=1,defval=mx_BIND_INTER_BYTE_TIMEOUT} */
	ok_v_bind_inter_key_timeout, /* {notempty=1,posnum=1} */
ok_b_bsdannounce, /* {obsolete=1} */
	ok_b_bsdcompat,
	ok_b_bsdflags,
	ok_b_bsdheadline,
	ok_b_bsdmsgs,
	ok_b_bsdorder,
	ok_v_build_cc, /* {virt=VAL_BUILD_CC_ARRAY} */
	ok_v_build_ld, /* {virt=VAL_BUILD_LD_ARRAY} */
	ok_v_build_oauth_helper, /* {virt=VAL_BUILD_OAUTH_HELPER_ARRAY} */
	ok_v_build_os, /* {virt=VAL_BUILD_OS} */
	ok_v_build_rest, /* {virt=VAL_BUILD_REST_ARRAY} */

	ok_v_COLUMNS, /* {notempty=1,posnum=1,env=1} */
	ok_v_cols, /* {notempty=1,posnum=1} */
	/* Charset lowercase conversion handled via vip= */
	ok_v_charset_7bit, /* {vip=1,notempty=1,defval=CHARSET_7BIT} */
	/* Unused without mx_HAVE_ICONV, we use mx_var_oklook(CHARSET_8BIT_OKEY)! */
	ok_v_charset_8bit, /* {vip=1,notempty=1,defval=CHARSET_8BIT} */
	ok_v_charset_locale, /* {vip=1,nolopts=1,rdonly=1,nodel=1} */
	ok_v_charset_unknown_8bit, /* {vip=1,notempty=1} */
	ok_v_cmd, /* {posix=1,notempty=1} */
	ok_b_colour_disable,
	ok_b_colour_pager, /* {obsolete=1} */
	ok_v_contact_mail, /* {virt=VAL_CONTACT_MAIL} */
	ok_v_contact_web, /* {virt=VAL_CONTACT_WEB} */
	ok_v_content_description_forwarded_message, /* {\ } */
		/* {defval=mx_CONTENT_DESC_FORWARDED_MESSAGE} */
	ok_v_content_description_quote_attachment, /* {\ } */
		/* {defval=mx_CONTENT_DESC_QUOTE_ATTACHMENT} */
	ok_v_content_description_smime_message, /* {\ } */
		/* {defval=mx_CONTENT_DESC_SMIME_MESSAGE} */
	ok_v_content_description_smime_signature, /* {\ } */
		/* {defval=mx_CONTENT_DESC_SMIME_SIG} */
	ok_v_crt, /* {posix=1,posnum=1,i3val="",norc=1} */
	ok_v_customhdr, /* {vip=1} */

	ok_v_DEAD, /* {notempty=1,env=1,defval=VAL_DEAD} */
	ok_v_datefield, /* {i3val="%Y-%m-%d %H:%M"} */
	ok_v_datefield_markout_older, /* {i3val="%Y-%m-%d"} */
	ok_b_debug, /* {posix=1,vip=1} */
	ok_b_disposition_notification_send,
	ok_b_dot, /* {posix=1} */
	ok_b_dotlock_disable,
ok_b_dotlock_ignore_error, /* {obsolete=1} */

	ok_v_EDITOR, /* {env=1,notempty=1,defval=VAL_EDITOR} */
	ok_v_editalong,
	ok_b_editheaders, /* {i3val=TRU1,norc=1} */
	ok_b_emptystart, /* {i3val=TRU1,norc=1} */
ok_v_encoding, /* {obsolete=1} */
	ok_b_errexit,
	ok_v_errors_limit, /* {notempty=1,posnum=1,defval=VAL_ERRORS_LIMIT} */
	ok_v_escape, /* {posix=1,defval=n_ESCAPE} */
	ok_v_expandaddr,
	ok_v_expandaddr_domaincheck, /* {notempty=1} */
	ok_v_expandargv,
	ok_b_expert,

	ok_v_features, /* {virt=VAL_FEATURES} */
	ok_b_flipr, /* {posix=1} */
	ok_v_folder, /* {posix=1,vip=1} */
	ok_v_folder_resolved, /* {rdonly=1,nodel=1} */
ok_v_folder_hook, /* {obsolete=1} */
	ok_b_followup_to, /* {i3val=TRU1,norc=1} */
	ok_b_followup_to_add_cc,
	ok_v_followup_to_honour, /* {i3val="ask-yes",norc=1} */
	ok_b_forward_add_cc,
	ok_b_forward_as_attachment,
	ok_v_forward_inject_head,
	ok_v_forward_inject_tail,
	ok_v_from, /* {vip=1} */
	ok_b_fullnames, /* {i3val=TRU1,norc=1} */
ok_v_fwdheading, /* {obsolete=1} */

	ok_v_HOME, /* {vip=1,nodel=1,notempty=1,import=1} */
	ok_b_header, /* {posix=1,i3val=TRU1} */
	ok_v_headline,
	ok_v_headline_bidi,
	ok_b_headline_plain,
	ok_v_history_file,
	ok_v_history_gabby, /* {i3val="all",norc=1} */
	ok_b_history_gabby_persist,
	ok_v_history_size, /* {notempty=1,posnum=1} */
	ok_b_hold, /* {posix=1,i3val=TRU1} */
	ok_v_hostname, /* {vip=1} */

	ok_b_iconv_disable,
	ok_b_idna_disable,
	ok_v_ifs, /* {vip=1,defval=" \t\n"} */
	ok_v_ifs_ws, /* {vip=1,rdonly=1,nodel=1,i3val=" \t\n"} */
	ok_b_ignore, /* {posix=1} */
	ok_b_ignoreeof, /* {posix=1} */
	ok_v_inbox,
	ok_v_indentprefix, /* {posix=1,defval="> ",rc=\t} */

	ok_b_keep, /* {posix=1,i3val=TRU1} */
	ok_b_keepsave, /* {posix=1,i3val=TRU1} */

	ok_v_LANG, /* {vip=1,env=1,notempty=1} */
	ok_v_LC_ALL, /* {name=LC_ALL,vip=1,env=1,notempty=1} */
	ok_v_LC_CTYPE, /* {name=LC_CTYPE,vip=1,env=1,notempty=1} */
	ok_v_LINES, /* {notempty=1,posnum=1,env=1} */
	ok_v_LISTER, /* {env=1,notempty=1,defval=VAL_LISTER} */
	ok_v_LOGNAME, /* {rdonly=1,import=1} */
	ok_v_line_editor_config, /* {notempty=1,i3val=VAL_MLE_CONFIG} */
	ok_v_line_editor_cpl_word_breaks, /* {\ } */
		/* {defval=n_LINE_EDITOR_CPL_WORD_BREAKS} */
	ok_b_line_editor_disable,
	ok_b_line_editor_no_defaults,
	ok_v_log_prefix, /* {nodel=1,i3val=VAL_UAGENT ": "} */

	ok_v_MAIL, /* {env=1} */
	ok_v_MAILCAPS, /* {import=1,defval=VAL_MAILCAPS} */
	ok_v_MAILRC, /* {import=1,notempty=1,defval=VAL_MAILRC} */
	ok_b_MAILX_NO_SYSTEM_RC, /* {name=MAILX_NO_SYSTEM_RC,import=1} */
	ok_v_MBOX, /* {env=1,notempty=1,defval=VAL_MBOX} */
	ok_v_mailbox_basename, /* {nolopts=1,rdonly=1,nodel=1} */
	ok_v_mailbox_display, /* {nolopts=1,rdonly=1,nodel=1} */
	ok_b_mailbox_read_only, /* {nolopts=1,rdonly=1,nodel=1} */
	ok_v_mailbox_resolved, /* {nolopts=1,rdonly=1,nodel=1} */
	ok_b_mailcap_disable,
	ok_v_mailx_extra_rc, /* {notempty=1} */
	ok_b_markanswered, /* {i3val=TRU1,norc=1} */
	ok_b_mbox_fcc_and_pcc, /* {i3val=TRU1} */
	ok_b_mbox_rfc4155,
	ok_b_memdebug, /* {vip=1} */
	ok_v_message_id,
	ok_b_message_id_disable,
	ok_v_message_inject_head,
	ok_v_message_inject_tail,
	ok_b_metoo, /* {posix=1} */
	ok_b_mime_allow_text_controls,
	ok_b_mime_alternative_favour_rich,
	ok_v_mime_counter_evidence, /* {posnum=1,i3val="0b1111",norc=1} */
	ok_v_mime_encoding,
	ok_b_mime_force_sendout, /* {i3val=TRU1} */
	ok_v_mimetypes_load_control,
	ok_v_mta, /* {notempty=1,defval=VAL_MTA} */
	ok_v_mta_aliases, /* {notempty=1} */
	ok_v_mta_arguments,
	ok_b_mta_no_default_arguments,
ok_b_mta_no_receiver_arguments, /* {obsolete=1} */
	ok_b_mta_no_recipient_arguments,
	ok_v_mta_argv0, /* {notempty=1,defval=VAL_MTA_ARGV0} */
	ok_b_mta_bcc_ok,

ok_v_NAIL_EXTRA_RC, /* {name=NAIL_EXTRA_RC,env=1,notempty=1,obsolete=1} */
ok_b_NAIL_NO_SYSTEM_RC, /* {name=NAIL_NO_SYSTEM_RC,import=1,obsolete=1} */
ok_v_NAIL_HEAD, /* {name=NAIL_HEAD,obsolete=1} */
ok_v_NAIL_HISTFILE, /* {name=NAIL_HISTFILE,obsolete=1} */
ok_v_NAIL_HISTSIZE, /* {name=NAIL_HISTSIZE,notempty=1,num=1,obsolete=1} */
ok_v_NAIL_TAIL, /* {name=NAIL_TAIL,obsolete=1} */
	ok_v_NETRC, /* {env=1,notempty=1,defval=VAL_NETRC} */
	ok_b_netrc_lookup, /* {chain=1} */
	ok_v_netrc_pipe,
	ok_v_newfolders,
	ok_v_newmail,

	ok_v_obsoletion, /* {vip=1,notempty=1,posnum=1} */
	ok_v_on_account_cleanup, /* {notempty=1} */
	ok_v_on_compose_cleanup, /* {notempty=1} */
	ok_v_on_compose_embed, /* {notempty=1} */
	ok_v_on_compose_enter, /* {notempty=1} */
	ok_v_on_compose_leave, /* {notempty=1} */
ok_v_on_compose_splice, /* {notempty=1,obsolete=1} */
ok_v_on_compose_splice_shell, /* {notempty=1,obsolete=1} */
	ok_v_on_history_addition, /* {notempty=1} */
	ok_v_on_mailbox_event, /* {notempty=1} */
	ok_v_on_main_loop_tick, /* {notempty=1} */
	ok_v_on_program_exit, /* {notempty=1} */
	ok_v_on_resend_cleanup, /* {notempty=1} */
	ok_v_on_resend_enter, /* {notempty=1} */
	ok_b_outfolder, /* {posix=1} */

	ok_v_PAGER, /* {env=1,notempty=1,defval=VAL_PAGER} */
	ok_v_PATH, /* {nodel=1,import=1} */
	/* XXX POSIXLY_CORRECT->posix: needs initial call via main()! */
	ok_b_POSIXLY_CORRECT, /* {vip=1,import=1,name=POSIXLY_CORRECT} */
	ok_b_page, /* {posix=1} */
	ok_v_password, /* {chain=1} */
ok_b_piperaw, /* {obsolete=1} */
	ok_v_pop3_auth, /* {chain=1} */
	ok_b_pop3_bulk_load,
	ok_v_pop3_keepalive, /* {notempty=1,posnum=1} */
	ok_b_pop3_no_apop, /* {chain=1} */
	ok_b_pop3_use_starttls, /* {chain=1} */
	ok_b_posix, /* {vip=1} */
	ok_b_print_alternatives,
	ok_v_prompt, /* {posix=1,i3val="? "} */
	ok_v_prompt2, /* {i3val=".. "} */

	ok_b_quiet, /* {posix=1} */
	ok_v_quote, /* {i3val="",norc=1} */
	ok_b_quote_add_cc,
	ok_b_quote_as_attachment,
	ok_v_quote_chars, /* {vip=1,notempty=1,defval=">|}:"} */
	ok_v_quote_fold,
	ok_v_quote_inject_head,
	ok_v_quote_inject_tail,

	ok_b_r_option_implicit,
	ok_b_recipients_in_cc, /* {i3val=TRU1,norc=1} */
	ok_v_record, /* {posix=1} */
	ok_b_record_files,
	ok_b_record_resent,
	ok_b_reply_in_same_charset,
	ok_v_reply_strings,
ok_v_replyto, /* {vip=1,obsolete=1,notempty=1} */
	ok_v_reply_to, /* {vip=1,notempty=1} */
	ok_v_reply_to_honour, /* {i3val="ask-yes",norc=1} */
	ok_v_reply_to_swap_in,
	ok_b_rfc822_body_from_, /* {name=rfc822-body-from_} */

	ok_v_SHELL, /* {import=1,notempty=1,defval=VAL_SHELL} */
ok_b_SYSV3, /* {env=1,obsolete=1} */
	ok_b_save, /* {posix=1,i3val=TRU1} */
	ok_v_screen, /* {posix=1,notempty=1,posnum=1} */
	ok_b_searchheaders,
	/* Charset lowercase conversion handled via vip= */
	ok_v_sendcharsets, /* {vip=1} */
	ok_b_sendcharsets_else_ttycharset,
	ok_v_sender, /* {vip=1} */
ok_v_sendmail, /* {obsolete=1} */
ok_v_sendmail_arguments, /* {obsolete=1} */
ok_b_sendmail_no_default_arguments, /* {obsolete=1} */
ok_v_sendmail_progname, /* {obsolete=1} */
	ok_v_sendwait, /* {posix=1,i3val=""} */
	ok_b_showlast,
	ok_b_showname, /* {i3val=TRU1,norc=1} */
	ok_b_showto, /* {posix=1,i3val=TRU1,norc=1} */
	ok_v_Sign, /* {posix=1} */
	ok_v_sign, /* {posix=1} */
ok_v_signature, /* {obsolete=1} */
	ok_b_skipemptybody, /* {vip=1} */
	ok_v_smime_ca_dir,
	ok_v_smime_ca_file,
	ok_v_smime_ca_flags,
	ok_b_smime_ca_no_defaults,
	ok_v_smime_cipher, /* {chain=1} */
	ok_v_smime_crl_dir,
	ok_v_smime_crl_file,
	ok_v_smime_encrypt, /* {chain=1} */
	ok_b_smime_force_encryption,
ok_b_smime_no_default_ca, /* {obsolete=1} */
	ok_b_smime_sign,
	ok_v_smime_sign_cert, /* {chain=1} */
	ok_v_smime_sign_digest, /* {chain=1} */
	ok_v_smime_sign_include_certs, /* {chain=1} */
ok_v_smime_sign_message_digest, /* {chain=1,obsolete=1} */
ok_v_smtp, /* {obsolete=1} */
ok_v_smtp_auth, /* {chain=1,obsolete=1} */
ok_v_smtp_auth_password, /* {obsolete=1} */
ok_v_smtp_auth_user, /* {obsolete=1} */
	ok_v_smtp_config, /* {chain=1} */
	ok_v_smtp_from, /* {vip=1,chain=1} */
ok_v_smtp_hostname, /* {vip=1,chain=1,obsolete=1} */
ok_b_smtp_use_starttls, /* {chain=1,obsolete=1} */
	ok_v_SOCKS5_PROXY, /* {vip=1,import=1,notempty=1,name=SOCKS5_PROXY} */
	ok_v_SOURCE_DATE_EPOCH, /* {\ } */
		/* {name=SOURCE_DATE_EPOCH,rdonly=1,import=1,notempty=1,posnum=1} */
	ok_v_socket_connect_timeout, /* {posnum=1} */
	ok_v_socks_proxy, /* {vip=1,chain=1,notempty=1} */
	ok_v_spam_interface,
	ok_v_spam_maxsize, /* {notempty=1,posnum=1} */
	ok_v_spamc_command,
	ok_v_spamc_arguments,
	ok_v_spamc_user,
	ok_v_spamfilter_ham,
	ok_v_spamfilter_noham,
	ok_v_spamfilter_nospam,
	ok_v_spamfilter_rate,
	ok_v_spamfilter_rate_scanscore,
	ok_v_spamfilter_spam,
ok_v_ssl_ca_dir, /* {chain=1,obsolete=1} */
ok_v_ssl_ca_file, /* {chain=1,obsolete=1} */
ok_v_ssl_ca_flags, /* {chain=1,obsolete=1} */
ok_b_ssl_ca_no_defaults, /* {chain=1,obsolete=1} */
ok_v_ssl_cert, /* {chain=1,obsolete=1} */
ok_v_ssl_cipher_list, /* {chain=1,obsolete=1} */
ok_v_ssl_config_file, /* {obsolete=1} */
ok_v_ssl_config_module, /* {chain=1,obsolete=1} */
ok_v_ssl_config_pairs, /* {chain=1,obsolete=1} */
ok_v_ssl_curves, /* {chain=1,obsolete=1} */
ok_v_ssl_crl_dir, /* {obsolete=1} */
ok_v_ssl_crl_file, /* {obsolete=1} */
ok_v_ssl_features, /* {virt=VAL_TLS_FEATURES,obsolete=1} */
ok_v_ssl_key, /* {chain=1,obsolete=1} */
ok_v_ssl_method, /* {chain=1,obsolete=1} */
ok_b_ssl_no_default_ca, /* {obsolete=1} */
ok_v_ssl_protocol, /* {chain=1,obsolete=1} */
ok_v_ssl_rand_egd, /* {obsolete=1} */
ok_v_ssl_rand_file, /* {obsolete=1}*/
ok_v_ssl_verify, /* {chain=1,obsolete=1} */
	ok_v_stealthmua,
	ok_v_system_mailrc, /* {virt=VAL_SYSCONFDIR "/" VAL_SYSCONFRC} */

	ok_v_TERM, /* {env=1} */
	ok_v_TMPDIR, /* {import=1,vip=1,notempty=1,defval=VAL_TMPDIR} */
	ok_v_termcap,
	ok_v_termcap_ca_mode,
	ok_b_termcap_disable,
	ok_v_tls_ca_dir, /* {chain=1} */
	ok_v_tls_ca_file, /* {chain=1} */
	ok_v_tls_ca_flags, /* {chain=1} */
	ok_b_tls_ca_no_defaults, /* {chain=1} */
	ok_v_tls_config_file,
	ok_v_tls_config_module, /* {chain=1} */
	ok_v_tls_config_pairs, /* {chain=1} */
	ok_v_tls_crl_dir,
	ok_v_tls_crl_file,
	ok_v_tls_features, /* {virt=VAL_TLS_FEATURES} */
	ok_v_tls_fingerprint, /* {chain=1} */
	ok_v_tls_fingerprint_digest, /* {chain=1} */
	ok_v_tls_rand_file,
	ok_v_tls_verify, /* {chain=1} */
	ok_v_toplines, /* {posix=1,notempty=1,num=1,defval="5"} */
	ok_b_topsqueeze,
	/* Charset lowercase conversion handled via vip= */
	ok_v_ttycharset, /* {vip=1,notempty=1,defval=CHARSET_8BIT} */
	ok_v_ttycharset_detect, /* {vip=1} */
	ok_b_typescript_mode, /* {vip=1} */

	ok_v_USER, /* {rdonly=1,import=1} */
	ok_v_umask, /* {vip=1,nodel=1,posnum=1,i3val="0077"} */
	ok_v_user, /* {notempty=1,chain=1} */

	ok_v_VISUAL, /* {env=1,notempty=1,defval=VAL_VISUAL} */
	ok_v_v15_compat, /* {i3val="y"} */
	ok_v_verbose, /* {vip=1,posnum=1} */
	ok_v_version, /* {virt=mx_VERSION} */
	ok_v_version_date, /* {virt=mx_VERSION_DATE} */
	ok_v_version_hexnum, /* {virt=mx_VERSION_HEXNUM,posnum=1} */
	ok_v_version_major, /* {virt=mx_VERSION_MAJOR,posnum=1} */
	ok_v_version_minor, /* {virt=mx_VERSION_MINOR,posnum=1} */
	ok_v_version_update, /* {virt=mx_VERSION_UPDATE,posnum=1} */

	ok_b_writebackedited

,	/* Obsolete IMAP related non-sorted */
ok_b_disconnected, /* {chain=1} */
ok_v_imap_auth, /* {chain=1} */
ok_v_imap_cache,
ok_v_imap_delim, /* {chain=1} */
ok_v_imap_keepalive, /* {chain=1} */
ok_v_imap_list_depth,
ok_b_imap_use_starttls, /* {chain=1} */

	ok_v__S_MAILX_TEST /* {name=_S_MAILX_TEST,env=1} */
}; /* }}} */
enum{
	mx_OKEYS_FIRST = ok_v_account, /* Truly accessible first */
	mx_OKEYS_MAX = ok_v__S_MAILX_TEST
};

enum okey_xlook_mode{
   OXM_PLAIN = 1u<<0, /* Plain key always tested */
   OXM_H_P = 1u<<1, /* Check PLAIN-.url_h_p */
   OXM_U_H_P = 1u<<2, /* Check PLAIN-.url_u_h_p */
   OXM_ALL = 0x7u
};

/* Do not use mx_var_* unless you *really* have to! */

/* Constant option key look/(un)set/clear */
EXPORT char *mx_var_oklook(enum okeys okey);
#define ok_blook(C) (mx_var_oklook(su_CONCAT(ok_b_, C)) != NIL)
#define ok_vlook(C) mx_var_oklook(su_CONCAT(ok_v_, C))

EXPORT boole mx_var_okset(enum okeys okey, up val);
#define ok_bset(C) mx_var_okset(su_CONCAT(ok_b_, C), (up)TRU1)
#define ok_vset(C,V) mx_var_okset(su_CONCAT(ok_v_, C), (up)(V))

EXPORT boole mx_var_okclear(enum okeys okey);
#define ok_bclear(C) mx_var_okclear(su_CONCAT(ok_b_, C))
#define ok_vclear(C) mx_var_okclear(su_CONCAT(ok_v_, C))

/* Variable option key lookup/(un)set/clear.
 * If try_getenv is true we will getenv(3) _if_ vokey is not a known enum okey;
 * it will also cause obsoletion messages only for doing lookup (once).
 * _vexplode() is to be used by the shell expansion stuff when encountering
 * $@/$^@ in double-quotes, in order to provide sh(1)ell compatible behavior;
 * it returns whether there are any elements in argv (*cookie): TRUM1 is
 * returned if all elements are empty strings (elements may point to static
 * constant and non-aligned data).
 * Calling vset with val==NIL is a clear request */
EXPORT char const *mx_var_vlook(char const *vokey, boole try_getenv);
EXPORT boole mx_var_vexplode(void const **cookie, boole rset);
EXPORT boole mx_var_vset(char const *vokey, up val, enum mx_scope scope);

/* Special case to handle the typical [xy-USER@HOST,] xy-HOST and plain xy
 * variable chains; oxm is a bitmix which tells which combinations to test */
#ifdef mx_HAVE_NET
EXPORT char *mx_var_xoklook(enum okeys okey, struct mx_url const *urlp, enum okey_xlook_mode oxm);
# define xok_BLOOK(C,URL,M) (mx_var_xoklook(C, URL, M) != NIL)
# define xok_VLOOK(C,URL,M) mx_var_xoklook(C, URL, M)
# define xok_blook(C,URL,M) xok_BLOOK(su_CONCAT(ok_b_, C), URL, M)
# define xok_vlook(C,URL,M) xok_VLOOK(su_CONCAT(ok_v_, C), URL, M)
#endif

#include <su/code-ou.h>
#endif /* mx_OKEYS_H */
/* s-itt-mode */
