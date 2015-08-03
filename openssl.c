/*@ S-nail - a mail user agent derived from Berkeley Mail.
 *@ OpenSSL functions. TODO this needs an overhaul -- there _are_ stack leaks!?
 *
 * Copyright (c) 2000-2004 Gunnar Ritter, Freiburg i. Br., Germany.
 * Copyright (c) 2012 - 2015 Steffen (Daode) Nurpmeso <sdaoden@users.sf.net>.
 */
/*
 * Copyright (c) 2002
 * Gunnar Ritter.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Gunnar Ritter
 *    and his contributors.
 * 4. Neither the name of Gunnar Ritter nor the names of his contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GUNNAR RITTER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL GUNNAR RITTER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#undef n_FILE
#define n_FILE openssl

#ifndef HAVE_AMALGAMATION
# include "nail.h"
#endif

EMPTY_FILE()
#ifdef HAVE_OPENSSL
#include <sys/socket.h>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>

#ifdef HAVE_OPENSSL_CONFIG
# include <openssl/conf.h>
#endif

#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
# include <dirent.h>
#endif

/*
 * OpenSSL client implementation according to: John Viega, Matt Messier,
 * Pravir Chandra: Network Security with OpenSSL. Sebastopol, CA 2002.
 */

/* Update manual on changes (for all those)! */
#define SSL_DISABLED_PROTOCOLS "-SSLv2"

#ifndef HAVE_OPENSSL_CONF_CTX /* TODO obsolete the fallback */
# ifndef SSL_OP_NO_SSLv2
#  define SSL_OP_NO_SSLv2     0
# endif
# ifndef SSL_OP_NO_SSLv3
#  define SSL_OP_NO_SSLv3     0
# endif
# ifndef SSL_OP_NO_TLSv1
#  define SSL_OP_NO_TLSv1     0
# endif
# ifndef SSL_OP_NO_TLSv1_1
#  define SSL_OP_NO_TLSv1_1   0
# endif
# ifndef SSL_OP_NO_TLSv1_2
#  define SSL_OP_NO_TLSv1_2   0
# endif

  /* SSL_CONF_CTX and _OP_NO_SSL_MASK were both introduced with 1.0.2!?! */
# ifndef SSL_OP_NO_SSL_MASK
#  define SSL_OP_NO_SSL_MASK  \
   (SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |\
   SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2)
# endif
#endif

#if HAVE_OPENSSL < 10100
# define _SSL_CLIENT_METHOD() SSLv23_client_method()
#else
# define _SSL_CLIENT_METHOD() TLS_client_method()
#endif

#ifdef HAVE_OPENSSL_STACK_OF
# define _STACKOF(X)          STACK_OF(X)
#else
# define _STACKOF(X)          /*X*/STACK
#endif

enum ssl_state {
   SS_INIT           = 1<<0,
   SS_RAND_INIT      = 1<<1,
   SS_EXIT_HDL       = 1<<2,
   SS_CONF_LOAD      = 1<<3,
   SS_ALGO_LOAD      = 1<<4,

   SS_VERIFY_ERROR   = 1<<7
};

/* We go for the OpenSSL v1.0.2+ SSL_CONF_CTX if available even if that means
 * that the library does internally what we'd otherwise do ourselfs.
 * Eventually we can drop the direct use cases */
enum ssl_conf_type {
   SCT_CERTIFICATE,
   SCT_CIPHER_STRING,
   SCT_PRIVATE_KEY,
   SCT_OPTIONS,
   SCT_PROTOCOL
};

struct ssl_method { /* TODO obsolete */
   char const  sm_name[8];
   char const  sm_map[16];
};

#ifndef HAVE_OPENSSL_CONF_CTX /* TODO obsolete the fallback */
struct ssl_protocol {
   char const  *sp_name;
   sl_i        sp_flag;
};
#endif

struct smime_cipher {
   char const        sc_name[8];
   EVP_CIPHER const  *(*sc_fun)(void);
};

struct smime_digest {
   char const     sd_name[8];
   EVP_MD const   *(*sd_fun)(void);
};

/* Supported SSL/TLS methods: update manual on change! */

static struct ssl_method const   _ssl_methods[] = { /* TODO obsolete */
   {"auto",    "ALL,-SSLv2"},
   {"ssl3",    "-ALL,SSLv3"},
   {"tls1",    "-ALL,TLSv1"},
   {"tls1.1",  "-ALL,TLSv1.1"},
   {"tls1.2",  "-ALL,TLSv1.2"}
};

/* Update manual on change! */
#ifndef HAVE_OPENSSL_CONF_CTX /* TODO obsolete the fallback */
static struct ssl_protocol const _ssl_protocols[] = {
   {"ALL",     SSL_OP_NO_SSL_MASK},
   {"TLSv1.2", SSL_OP_NO_TLSv1_2},
   {"TLSv1.1", SSL_OP_NO_TLSv1_1},
   {"TLSv1",   SSL_OP_NO_TLSv1},
   {"SSLv3",   SSL_OP_NO_SSLv3},
   {"SSLv2",   0}
};
#endif

/* Supported S/MIME cipher algorithms */
static struct smime_cipher const _smime_ciphers[] = { /* Manual!! */
#ifndef OPENSSL_NO_AES
# define _SMIME_DEFAULT_CIPHER   EVP_aes_128_cbc   /* According to RFC 5751 */
   {"aes128",  &EVP_aes_128_cbc},
   {"aes256",  &EVP_aes_256_cbc},
   {"aes192",  &EVP_aes_192_cbc},
#endif
#ifndef OPENSSL_NO_DES
# ifndef _SMIME_DEFAULT_CIPHER
#  define _SMIME_DEFAULT_CIPHER  EVP_des_ede3_cbc
# endif
   {"des3",    &EVP_des_ede3_cbc},
   {"des",     &EVP_des_cbc},
#endif
};
#ifndef _SMIME_DEFAULT_CIPHER
# error Your OpenSSL library does not include the necessary
# error cipher algorithms that are required to support S/MIME
#endif

#ifndef OPENSSL_NO_AES
static struct smime_cipher const _smime_ciphers_obs[] = { /* TODO obsolete */
   {"aes-128", &EVP_aes_128_cbc},
   {"aes-256", &EVP_aes_256_cbc},
   {"aes-192", &EVP_aes_192_cbc}
};
#endif

/* Supported S/MIME message digest algorithms */
static struct smime_digest const _smime_digests[] = { /* Manual!! */
#define _SMIME_DEFAULT_DIGEST    EVP_sha1 /* According to RFC 5751 */
#define _SMIME_DEFAULT_DIGEST_S  "sha1"
   {"sha1",    &EVP_sha1},
   {"sha256",  &EVP_sha256},
   {"sha512",  &EVP_sha512},
   {"sha384",  &EVP_sha384},
   {"sha224",  &EVP_sha224},
#ifndef OPENSSL_NO_MD5
   {"md5",     &EVP_md5},
#endif
};

static enum ssl_state   _ssl_state;
static size_t           _ssl_msgno;

static int        _ssl_rand_init(void);
static void       _ssl_init(void);
#if defined HAVE_DEVEL && defined HAVE_OPENSSL_MEMHOOKS && defined HAVE_DEBUG
static void       _ssl_free(void *vp);
#endif
#ifdef HAVE_SSL_ALL_ALGORITHMS
static void       _ssl_load_algos(void);
#endif
#if defined HAVE_OPENSSL_CONFIG || defined HAVE_SSL_ALL_ALGORITHMS
static void       _ssl_atexit(void);
#endif

static bool_t     _ssl_parse_asn1_time(ASN1_TIME *atp,
                     char *bdat, size_t blen);
static int        _ssl_verify_cb(int success, X509_STORE_CTX *store);

/* SSL_CTX configuration */
static void *     _ssl_conf_setup(SSL_CTX *ctxp);
static bool_t     _ssl_conf(void *confp, enum ssl_conf_type sct,
                     char const *value);
static bool_t     _ssl_conf_finish(void **confp, bool_t error);

static bool_t     _ssl_load_verifications(SSL_CTX *ctxp);

static enum okay  ssl_check_host(struct sock *sp, struct url const *urlp);

static int        smime_verify(struct message *m, int n, _STACKOF(X509) *chain,
                        X509_STORE *store);
static EVP_CIPHER const * _smime_cipher(char const *name);
static int        ssl_password_cb(char *buf, int size, int rwflag,
                     void *userdata);
static FILE *     smime_sign_cert(char const *xname, char const *xname2,
                     bool_t dowarn);
static char *     _smime_sign_include_certs(char const *name);
static bool_t     _smime_sign_include_chain_creat(_STACKOF(X509) **chain,
                     char const *cfiles);
static EVP_MD const * _smime_sign_digest(char const *name,
                        char const **digname);
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay  load_crl1(X509_STORE *store, char const *name);
#endif
static enum okay  load_crls(X509_STORE *store, enum okeys fok, enum okeys dok);

static int
_ssl_rand_init(void)
{
   char *cp, *x;
   int state = 0;
   NYD_ENTER;

#ifdef HAVE_OPENSSL_RAND_EGD
   if ((cp = ok_vlook(ssl_rand_egd)) != NULL) {
      if ((x = file_expand(cp)) == NULL || RAND_egd(cp = x) == -1)
         n_err(_("Entropy daemon at \"%s\" not available\n"), cp);
      else
         state = 1;
   } else
#endif
   if ((cp = ok_vlook(ssl_rand_file)) != NULL) {
      if ((x = file_expand(cp)) == NULL || RAND_load_file(cp = x, 1024) == -1)
         n_err(_("Entropy file at \"%s\" not available\n"), cp);
      else {
         struct stat st;

         if (!stat(cp, &st) && S_ISREG(st.st_mode) && !access(cp, W_OK)) {
            if (RAND_write_file(cp) == -1) {
               n_err(_("Writing entropy data to \"%s\" failed\n"), cp);
            }
         }
         state = 1;
      }
   }
   NYD_LEAVE;
   return state;
}

static void
_ssl_init(void)
{
#ifdef HAVE_OPENSSL_CONFIG
   char const *cp;
#endif
   NYD_ENTER;

   if (!(_ssl_state & SS_INIT)) {
#if defined HAVE_DEVEL && defined HAVE_OPENSSL_MEMHOOKS
# ifdef HAVE_DEBUG
      CRYPTO_set_mem_ex_functions(&smalloc, &srealloc, &_ssl_free);
# else
      CRYPTO_set_mem_functions(&smalloc, &srealloc, &free);
# endif
#endif
      SSL_library_init();
      SSL_load_error_strings();
      _ssl_state |= SS_INIT;
   }

   /* Load openssl.cnf or whatever was given in *ssl-config-file* */
#ifdef HAVE_OPENSSL_CONFIG
   if (!(_ssl_state & SS_CONF_LOAD) &&
         (cp = ok_vlook(ssl_config_file)) != NULL) {
      ul_i flags = CONF_MFLAGS_IGNORE_MISSING_FILE;

      if (*cp == '\0') {
         cp = NULL;
         flags = 0;
      }
      if (CONF_modules_load_file(cp, uagent, flags) == 1) {
         _ssl_state |= SS_CONF_LOAD;
         if (!(_ssl_state & SS_EXIT_HDL)) {
            _ssl_state |= SS_EXIT_HDL;
            atexit(&_ssl_atexit); /* TODO generic program-wide event mech. */
         }
      } else
         ssl_gen_err(_("Ignoring CONF_modules_load_file() load error"));
   }
#endif

   if (!(_ssl_state & SS_RAND_INIT) && _ssl_rand_init())
      _ssl_state |= SS_RAND_INIT;
   NYD_LEAVE;
}

#if defined HAVE_DEVEL && defined HAVE_OPENSSL_MEMHOOKS && defined HAVE_DEBUG
static void
_ssl_free(void *vp)
{
   NYD_ENTER;
   if (vp != NULL)
      free(vp);
   NYD_LEAVE;
}
#endif

#ifdef HAVE_SSL_ALL_ALGORITHMS
static void
_ssl_load_algos(void)
{
   NYD_ENTER;
   if (!(_ssl_state & SS_ALGO_LOAD)) {
      _ssl_state |= SS_ALGO_LOAD;
      OpenSSL_add_all_algorithms();

      if (!(_ssl_state & SS_EXIT_HDL)) {
         _ssl_state |= SS_EXIT_HDL;
         atexit(&_ssl_atexit); /* TODO generic program-wide event mech. */
      }
   }
   NYD_LEAVE;
}
#endif

#if defined HAVE_OPENSSL_CONFIG || defined HAVE_SSL_ALL_ALGORITHMS
static void
_ssl_atexit(void)
{
   NYD_ENTER;
# ifdef HAVE_SSL_ALL_ALGORITHMS
   if (_ssl_state & SS_ALGO_LOAD)
      EVP_cleanup();
# endif
# ifdef HAVE_OPENSSL_CONFIG
   if (_ssl_state & SS_CONF_LOAD)
      CONF_modules_free();
# endif
   NYD_LEAVE;
}
#endif

static bool_t
_ssl_parse_asn1_time(ASN1_TIME *atp, char *bdat, size_t blen)
{
   BIO *mbp;
   char *mcp;
   long l;
   NYD_ENTER;

   mbp = BIO_new(BIO_s_mem());

   if (ASN1_TIME_print(mbp, atp) && (l = BIO_get_mem_data(mbp, &mcp)) > 0)
      snprintf(bdat, blen, "%.*s", (int)l, mcp);
   else {
      snprintf(bdat, blen, _("Bogus certificate date: %.*s"),
         /*is (int)*/atp->length, (char const*)atp->data);
      mcp = NULL;
   }

   BIO_free(mbp);
   NYD_LEAVE;
   return (mcp != NULL);
}

static int
_ssl_verify_cb(int success, X509_STORE_CTX *store)
{
   char data[256];
   X509 *cert;
   int rv = TRU1;
   NYD_ENTER;

   if (success && !(options & OPT_VERB))
      goto jleave;

   if (_ssl_msgno != 0) {
      n_err(_("Message %lu:\n"), (ul_i)_ssl_msgno);
      _ssl_msgno = 0;
   }
   n_err(_(" Certificate depth %d %s\n"),
      X509_STORE_CTX_get_error_depth(store), (success ? "" : _("ERROR")));

   if ((cert = X509_STORE_CTX_get_current_cert(store)) != NULL) {
      X509_NAME_oneline(X509_get_subject_name(cert), data, sizeof data);
      n_err(_("  subject = %s\n"), data);

      _ssl_parse_asn1_time(X509_get_notBefore(cert), data, sizeof data);
      n_err(_("  notBefore = %s\n"), data);

      _ssl_parse_asn1_time(X509_get_notAfter(cert), data, sizeof data);
      n_err(_("  notAfter = %s\n"), data);

      X509_NAME_oneline(X509_get_issuer_name(cert), data, sizeof data);
      n_err(_("  issuer = %s\n"), data);
   }

   if (!success) {
      int err = X509_STORE_CTX_get_error(store);

      n_err(_("  err %i: %s\n"), err, X509_verify_cert_error_string(err));
      _ssl_state |= SS_VERIFY_ERROR;
   }

   if (!success && ssl_verify_decide() != OKAY)
      rv = FAL0;
jleave:
   NYD_LEAVE;
   return rv;
}

#ifdef HAVE_OPENSSL_CONF_CTX
static void *
_ssl_conf_setup(SSL_CTX *ctxp)
{
   SSL_CONF_CTX *sccp;
   NYD_ENTER;

   if ((sccp = SSL_CONF_CTX_new()) != NULL) {
      SSL_CONF_CTX_set_flags(sccp,
         SSL_CONF_FLAG_FILE | SSL_CONF_FLAG_CLIENT |
         SSL_CONF_FLAG_CERTIFICATE | SSL_CONF_FLAG_SHOW_ERRORS);

      SSL_CONF_CTX_set_ssl_ctx(sccp, ctxp);
   } else
      ssl_gen_err(_("SSL_CONF_CTX_new() failed"));

   NYD_LEAVE;
   return sccp;
}

static bool_t
_ssl_conf(void *confp, enum ssl_conf_type sct, char const *value)
{
   int rv;
   char const *cmsg;
   SSL_CONF_CTX *sccp = (SSL_CONF_CTX*)confp;
   NYD_ENTER;

   switch (sct) {
   case SCT_CERTIFICATE:
      cmsg = "ssl-cert";
      rv = SSL_CONF_cmd(sccp, "Certificate", value);
      break;
   case SCT_CIPHER_STRING:
      cmsg = "ssl-cipher-list";
      rv = SSL_CONF_cmd(sccp, "CipherString", value);
      break;
   case SCT_PRIVATE_KEY:
      cmsg = "ssl-key";
      rv = SSL_CONF_cmd(sccp, "PrivateKey", value);
      break;
   default:
   case SCT_OPTIONS:
      cmsg = "ssl-options";
      rv = SSL_CONF_cmd(sccp, "Options", "Bugs");
      break;
   case SCT_PROTOCOL:
      cmsg = "ssl-protocol";
      rv = SSL_CONF_cmd(sccp, "Protocol", value);
      break;
   }

   if (rv == 2)
      rv = 0;
   else {
      if (rv == 0)
         ssl_gen_err(_("SSL_CONF_CTX_cmd() failed for *%s*"), cmsg);
      else
         n_err(_("%s: *%s* implementation error, please report this\n"),
            uagent, cmsg);
      rv = 1;
   }

   NYD_LEAVE;
   return (rv == 0);
}

static bool_t
_ssl_conf_finish(void **confp, bool_t error)
{
   SSL_CONF_CTX **sccp = (SSL_CONF_CTX**)confp;
   bool_t rv;
   NYD_ENTER;

   if (!(rv = error))
      rv = (SSL_CONF_CTX_finish(*sccp) != 0);

   SSL_CONF_CTX_free(*sccp);

   *sccp = NULL;
   NYD_LEAVE;
   return rv;
}

#else /* HAVE_OPENSSL_CONF_CTX */
static void *
_ssl_conf_setup(SSL_CTX* ctxp)
{
   return ctxp;
}

static bool_t
_ssl_conf(void *confp, enum ssl_conf_type sct, char const *value)
{
   SSL_CTX *ctxp = (SSL_CTX*)confp;
   NYD_ENTER;

   switch (sct) {
   case SCT_CERTIFICATE:
      if (SSL_CTX_use_certificate_chain_file(ctxp, value) != 1) {
         ssl_gen_err(_("Can't load certificate from file \"%s\"\n"), value);
         confp = NULL;
      }
      break;
   case SCT_CIPHER_STRING:
      if (SSL_CTX_set_cipher_list(ctxp, value) != 1) {
         ssl_gen_err(_("Invalid cipher string: \"%s\"\n"), value);
         confp = NULL;
      }
      break;
   case SCT_PRIVATE_KEY:
      if (SSL_CTX_use_PrivateKey_file(ctxp, value, SSL_FILETYPE_PEM) != 1) {
         ssl_gen_err(_("Can't load private key from file \"%s\"\n"), value);
         confp = NULL;
      }
      break;
   case SCT_OPTIONS:
      /* "Options"="Bugs" TODO *ssl-options* */
      SSL_CTX_set_options(ctxp, SSL_OP_ALL);
      break;
   case SCT_PROTOCOL: {
      char *iolist, *cp, addin;
      size_t i;
      sl_i opts = 0;

      confp = NULL;
      for (iolist = cp = savestr(value);
            (cp = n_strsep(&iolist, ',', FAL0)) != NULL;) {
         if (*cp == '\0') {
            n_err(_("*ssl-protocol*: empty arguments are not supported\n"));
            goto jleave;
         }

         addin = TRU1;
         switch (cp[0]) {
         case '-': addin = FAL0; /* FALLTHRU */
         case '+': ++cp; /* FALLTHRU */
         default : break;
         }

         for (i = 0;;) {
            if (!asccasecmp(cp, _ssl_protocols[i].sp_name)) {
               /* We need to inverse the meaning of the _NO_s */
               if (!addin)
                  opts |= _ssl_protocols[i].sp_flag;
               else
                  opts &= ~_ssl_protocols[i].sp_flag;
               break;
            }
            if (++i < NELEM(_ssl_protocols))
               continue;
            n_err(_("*ssl-protocol*: unsupported value \"%s\"\n"), cp);
            goto jleave;
         }
      }
      confp = ctxp;
      SSL_CTX_set_options(ctxp, opts);
      break;
   }
   }
jleave:
   NYD_LEAVE;
   return (confp != NULL);
}

static bool_t
_ssl_conf_finish(void **confp, bool_t error)
{
   UNUSED(confp);
   UNUSED(error);
   return TRU1;
}
#endif /* !HAVE_OPENSSL_CONF_CTX */

static bool_t
_ssl_load_verifications(SSL_CTX *ctxp)
{
   char *ca_dir, *ca_file;
   X509_STORE *store;
   bool_t rv = FAL0;
   NYD_ENTER;

   if (ssl_verify_level == SSL_VERIFY_IGNORE) {
      rv = TRU1;
      goto jleave;
   }

   if ((ca_dir = ok_vlook(ssl_ca_dir)) != NULL)
      ca_dir = file_expand(ca_dir);
   if ((ca_file = ok_vlook(ssl_ca_file)) != NULL)
      ca_file = file_expand(ca_file);

   if ((ca_dir != NULL || ca_file != NULL) &&
         SSL_CTX_load_verify_locations(ctxp, ca_file, ca_dir) != 1) {
      char const *m1, *m2, *m3;

      if (ca_dir != NULL) {
         m1 = ca_dir;
         m2 = (ca_file != NULL) ? _(" or ") : "";
      } else
         m1 = m2 = "";
      m3 = (ca_file != NULL) ? ca_file : "";
      ssl_gen_err(_("Error loading %s%s%s\n"), m1, m2, m3);
      goto jleave;
   }

   if (!ok_blook(ssl_no_default_ca) &&
         SSL_CTX_set_default_verify_paths(ctxp) != 1) {
      ssl_gen_err(_("Error loading default CA locations\n"));
      goto jleave;
   }

   _ssl_state &= ~SS_VERIFY_ERROR;
   _ssl_msgno = 0;
   SSL_CTX_set_verify(ctxp, SSL_VERIFY_PEER, &_ssl_verify_cb);
   store = SSL_CTX_get_cert_store(ctxp);
   load_crls(store, ok_v_ssl_crl_file, ok_v_ssl_crl_dir);

   rv = TRU1;
jleave:
   NYD_LEAVE;
   return rv;
}

static enum okay
ssl_check_host(struct sock *sp, struct url const *urlp)
{
   char data[256];
   X509 *cert;
   _STACKOF(GENERAL_NAME) *gens;
   GENERAL_NAME *gen;
   X509_NAME *subj;
   enum okay rv = STOP;
   NYD_ENTER;

   if ((cert = SSL_get_peer_certificate(sp->s_ssl)) == NULL) {
      n_err(_("No certificate from \"%s\"\n"), urlp->url_h_p.s);
      goto jleave;
   }

   gens = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
   if (gens != NULL) {
      int i;

      for (i = 0; i < sk_GENERAL_NAME_num(gens); ++i) {
         gen = sk_GENERAL_NAME_value(gens, i);
         if (gen->type == GEN_DNS) {
            if (options & OPT_VERB)
               n_err(_("Comparing subject_alt_name: need<%s> is<%s>\n"),
                  urlp->url_host.s, (char*)gen->d.ia5->data);
            rv = rfc2595_hostname_match(urlp->url_host.s,
                  (char*)gen->d.ia5->data);
            if (rv == OKAY)
               goto jdone;
         }
      }
   }

   if ((subj = X509_get_subject_name(cert)) != NULL &&
         X509_NAME_get_text_by_NID(subj, NID_commonName, data, sizeof data)
            > 0) {
      data[sizeof data - 1] = '\0';
      if (options & OPT_VERB)
         n_err(_("Comparing commonName: need<%s> is<%s>\n"),
            urlp->url_host.s, data);
      rv = rfc2595_hostname_match(urlp->url_host.s, data);
   }

jdone:
   X509_free(cert);
jleave:
   NYD_LEAVE;
   return rv;
}

static int
smime_verify(struct message *m, int n, _STACKOF(X509) *chain, X509_STORE *store)
{
   char data[LINESIZE], *sender, *to, *cc, *cnttype;
   int rv, c, i, j;
   struct message *x;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   _STACKOF(X509) *certs;
   _STACKOF(GENERAL_NAME) *gens;
   X509 *cert;
   X509_NAME *subj;
   GENERAL_NAME *gen;
   NYD_ENTER;

   rv = 1;
   fp = NULL;
   fb = NULL;
   _ssl_state &= ~SS_VERIFY_ERROR;
   _ssl_msgno = (size_t)n;

   for (;;) {
      sender = getsender(m);
      to = hfield1("to", m);
      cc = hfield1("cc", m);
      cnttype = hfield1("content-type", m);

#undef _X
#undef _Y
#define _X     (sizeof("application/") -1)
#define _Y(X)  X, sizeof(X) -1
      if (cnttype && is_asccaseprefix(cnttype, "application/") &&
            (!ascncasecmp(cnttype + _X, _Y("pkcs7-mime")) ||
             !ascncasecmp(cnttype + _X, _Y("x-pkcs7-mime")))) {
#undef _Y
#undef _X
         if ((x = smime_decrypt(m, to, cc, 1)) == NULL)
            goto jleave;
         if (x != (struct message*)-1) {
            m = x;
            continue;
         }
      }

      if ((ip = setinput(&mb, m, NEED_BODY)) == NULL)
         goto jleave;
      size = m->m_size;
      break;
   }

   if ((fp = Ftmp(NULL, "smimever", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      n_perr(_("tempfile"), 0);
      goto jleave;
   }
   while (size-- > 0) {
      c = getc(ip);
      putc(c, fp);
   }
   fflush_rewind(fp);

   if ((fb = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_(
         "Error creating BIO verification object for message %d"), n);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object for message %d"), n);
      goto jleave;
   }
   if (PKCS7_verify(pkcs7, chain, store, pb, NULL, 0) != 1) {
      ssl_gen_err(_("Error verifying message %d"), n);
      goto jleave;
   }

   if (sender == NULL) {
      n_err(_("Warning: Message %d has no sender\n"), n);
      rv = 0;
      goto jleave;
   }

   certs = PKCS7_get0_signers(pkcs7, chain, 0);
   if (certs == NULL) {
      n_err(_("No certificates found in message %d\n"), n);
      goto jleave;
   }

   for (i = 0; i < sk_X509_num(certs); ++i) {
      cert = sk_X509_value(certs, i);
      gens = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
      if (gens != NULL) {
         for (j = 0; j < sk_GENERAL_NAME_num(gens); ++j) {
            gen = sk_GENERAL_NAME_value(gens, j);
            if (gen->type == GEN_EMAIL) {
               if (options & OPT_VERB)
                  n_err(_("Comparing subject_alt_name: need<%s> is<%s>)\n"),
                     sender, (char*)gen->d.ia5->data);
               if (!asccasecmp((char*)gen->d.ia5->data, sender))
                  goto jfound;
            }
         }
      }

      if ((subj = X509_get_subject_name(cert)) != NULL &&
            X509_NAME_get_text_by_NID(subj, NID_pkcs9_emailAddress,
               data, sizeof data) > 0) {
         data[sizeof data -1] = '\0';
         if (options & OPT_VERB)
            n_err(_("Comparing emailAddress: need<%s> is<%s>\n"),
               sender, data);
         if (!asccasecmp(data, sender))
            goto jfound;
      }
   }
   n_err(_("Message %d: certificate does not match <%s>\n"), n, sender);
   goto jleave;
jfound:
   rv = ((_ssl_state & SS_VERIFY_ERROR) != 0);
   if (!rv)
      printf(_("Message %d was verified successfully\n"), n);
jleave:
   if (fb != NULL)
      BIO_free(fb);
   if (fp != NULL)
      Fclose(fp);
   NYD_LEAVE;
   return rv;
}

static EVP_CIPHER const *
_smime_cipher(char const *name)
{
   EVP_CIPHER const *cipher;
   char *vn;
   char const *cp;
   size_t i;
   NYD_ENTER;

   vn = ac_alloc(i = strlen(name) + sizeof("smime-cipher-") -1 +1);
   snprintf(vn, (int)i, "smime-cipher-%s", name);
   cp = vok_vlook(vn);
   ac_free(vn);

   if (cp == NULL) {
      cipher = _SMIME_DEFAULT_CIPHER();
      goto jleave;
   }
   cipher = NULL;

   for (i = 0; i < NELEM(_smime_ciphers); ++i)
      if (!asccasecmp(_smime_ciphers[i].sc_name, cp)) {
         cipher = (*_smime_ciphers[i].sc_fun)();
         goto jleave;
      }
#ifndef OPENSSL_NO_AES
   for (i = 0; i < NELEM(_smime_ciphers_obs); ++i) /* TODO obsolete */
      if (!asccasecmp(_smime_ciphers_obs[i].sc_name, cp)) {
         OBSOLETE2(_("*smime-cipher* names with hyphens will vanish"), cp);
         cipher = (*_smime_ciphers_obs[i].sc_fun)();
         goto jleave;
      }
#endif

   /* Not a builtin algorithm, but we may have dynamic support for more */
#ifdef HAVE_SSL_ALL_ALGORITHMS
   _ssl_load_algos();
   if ((cipher = EVP_get_cipherbyname(cp)) != NULL)
      goto jleave;
#endif

   n_err(_("Invalid cipher(s): \"%s\"\n"), cp);
jleave:
   NYD_LEAVE;
   return cipher;
}

static int
ssl_password_cb(char *buf, int size, int rwflag, void *userdata)
{
   char *pass;
   size_t len;
   NYD_ENTER;
   UNUSED(rwflag);
   UNUSED(userdata);

   if ((pass = getpassword("PEM pass phrase:")) != NULL) {
      len = strlen(pass);
      if (UICMP(z, len, >=, size))
         len = size -1;
      memcpy(buf, pass, len);
      buf[len] = '\0';
   } else
      len = 0;
   NYD_LEAVE;
   return (int)len;
}

static FILE *
smime_sign_cert(char const *xname, char const *xname2, bool_t dowarn)
{
   char *vn, *cp;
   int vs;
   struct name *np;
   char const *name = xname, *name2 = xname2;
   FILE *fp = NULL;
   NYD_ENTER;

jloop:
   if (name) {
      np = lextract(name, GTO | GSKIN);
      while (np != NULL) {
         /* This needs to be more intelligent since it will currently take the
          * first name for which a private key is available regardless of
          * whether it is the right one for the message */
         vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-cert-%s", np->n_name);
         cp = vok_vlook(vn);
         ac_free(vn);
         if (cp != NULL)
            goto jopen;
         np = np->n_flink;
      }
      if (name2 != NULL) {
         name = name2;
         name2 = NULL;
         goto jloop;
      }
   }

   if ((cp = ok_vlook(smime_sign_cert)) == NULL)
      goto jerr;
jopen:
   if ((cp = file_expand(cp)) == NULL)
      goto jleave;
   if ((fp = Fopen(cp, "r")) == NULL)
      n_perr(cp, 0);
jleave:
   NYD_LEAVE;
   return fp;
jerr:
   if (dowarn)
      n_err(_("Could not find a certificate for %s%s%s\n"),
         xname, (xname2 != NULL ? _("or ") : ""),
         (xname2 != NULL ? xname2 : ""));
   goto jleave;
}

static char *
_smime_sign_include_certs(char const *name)
{
   char *rv;
   NYD_ENTER;

   /* See comments in smime_sign_cert() for algorithm pitfalls */
   if (name != NULL) {
      struct name *np;

      for (np = lextract(name, GTO | GSKIN); np != NULL; np = np->n_flink) {
         int vs;
         char *vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-include-certs-%s", np->n_name);
         rv = vok_vlook(vn);
         ac_free(vn);
         if (rv != NULL)
            goto jleave;
      }
   }
   rv = ok_vlook(smime_sign_include_certs);
jleave:
   NYD_LEAVE;
   return rv;
}

static bool_t
_smime_sign_include_chain_creat(_STACKOF(X509) **chain, char const *cfiles)
{
   X509 *tmp;
   FILE *fp;
   char *nfield, *cfield, *x;
   NYD_ENTER;

   *chain = sk_X509_new_null();

   for (nfield = savestr(cfiles);
         (cfield = n_strsep(&nfield, ',', TRU1)) != NULL;) {
      if ((x = file_expand(cfield)) == NULL ||
            (fp = Fopen(cfield = x, "r")) == NULL) {
         n_perr(cfiles, 0);
         goto jerr;
      }
      if ((tmp = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
         ssl_gen_err(_("Error reading certificate from \"%s\""), cfield);
         Fclose(fp);
         goto jerr;
      }
      sk_X509_push(*chain, tmp);
      Fclose(fp);
   }

   if (sk_X509_num(*chain) == 0) {
      n_err(_("*smime-sign-include-certs* defined but empty\n"));
      goto jerr;
   }
jleave:
   NYD_LEAVE;
   return (*chain != NULL);
jerr:
   sk_X509_pop_free(*chain, X509_free);
   *chain = NULL;
   goto jleave;
}

static EVP_MD const *
_smime_sign_digest(char const *name, char const **digname)
{
   EVP_MD const *digest;
   char const *cp;
   size_t i;
   NYD_ENTER;

   /* See comments in smime_sign_cert() for algorithm pitfalls */
   if (name != NULL) {
      struct name *np;

      for (np = lextract(name, GTO | GSKIN); np != NULL; np = np->n_flink) {
         int vs;
         char *vn = ac_alloc(vs = strlen(np->n_name) + 30);
         snprintf(vn, vs, "smime-sign-message-digest-%s", np->n_name);
         cp = vok_vlook(vn);
         ac_free(vn);
         if (cp != NULL)
            goto jhave_name;
      }
   }

   if ((cp = ok_vlook(smime_sign_message_digest)) == NULL) {
      digest = _SMIME_DEFAULT_DIGEST();
      *digname = _SMIME_DEFAULT_DIGEST_S;
      goto jleave;
   }

jhave_name:
   i = strlen(cp);
   {  char *x = salloc(i +1);
      i_strcpy(x, cp, i +1);
      cp = x;
   }
   *digname = cp;

   for (i = 0; i < NELEM(_smime_digests); ++i)
      if (!strcmp(_smime_digests[i].sd_name, cp)) {
         digest = (*_smime_digests[i].sd_fun)();
         goto jleave;
      }

   /* Not a builtin algorithm, but we may have dynamic support for more */
#ifdef HAVE_SSL_ALL_ALGORITHMS
   _ssl_load_algos();
   if ((digest = EVP_get_digestbyname(cp)) != NULL)
      goto jleave;
#endif

   n_err(_("Invalid message digest: \"%s\"\n"), cp);
   digest = NULL;
jleave:
   NYD_LEAVE;
   return digest;
}

#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
static enum okay
load_crl1(X509_STORE *store, char const *name)
{
   X509_LOOKUP *lookup;
   enum okay rv = STOP;
   NYD_ENTER;

   if (options & OPT_VERB)
      n_err(_("Loading CRL from \"%s\"\n"), name);
   if ((lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file())) == NULL) {
      ssl_gen_err(_("Error creating X509 lookup object"));
      goto jleave;
   }
   if (X509_load_crl_file(lookup, name, X509_FILETYPE_PEM) != 1) {
      ssl_gen_err(_("Error loading CRL from \"%s\""), name);
      goto jleave;
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* new OpenSSL */

static enum okay
load_crls(X509_STORE *store, enum okeys fok, enum okeys dok)
{
   char *crl_file, *crl_dir;
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   DIR *dirp;
   struct dirent *dp;
   char *fn = NULL;
   int fs = 0, ds, es;
#endif
   enum okay rv = STOP;
   NYD_ENTER;

   if ((crl_file = _var_oklook(fok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      if ((crl_file = file_expand(crl_file)) == NULL ||
            load_crl1(store, crl_file) != OKAY)
         goto jleave;
#else
      n_err(_("This OpenSSL version is too old to use CRLs\n"));
      goto jleave;
#endif
   }

   if ((crl_dir = _var_oklook(dok)) != NULL) {
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
      char *x;
      if ((x = file_expand(crl_dir)) == NULL ||
            (dirp = opendir(crl_dir = x)) == NULL) {
         n_perr(crl_dir, 0);
         goto jleave;
      }

      ds = strlen(crl_dir);
      fn = smalloc(fs = ds + 20);
      memcpy(fn, crl_dir, ds);
      fn[ds] = '/';
      while ((dp = readdir(dirp)) != NULL) {
         if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
               (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
            continue;
         if (dp->d_name[0] == '.')
            continue;
         if (ds + (es = strlen(dp->d_name)) + 2 < fs)
            fn = srealloc(fn, fs = ds + es + 20);
         memcpy(fn + ds + 1, dp->d_name, es + 1);
         if (load_crl1(store, fn) != OKAY) {
            closedir(dirp);
            free(fn);
            goto jleave;
         }
      }
      closedir(dirp);
      free(fn);
#else /* old OpenSSL */
      n_err(_("This OpenSSL version is too old to use CRLs\n"));
      goto jleave;
#endif
   }
#if defined X509_V_FLAG_CRL_CHECK && defined X509_V_FLAG_CRL_CHECK_ALL
   if (crl_file || crl_dir)
      X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
         X509_V_FLAG_CRL_CHECK_ALL);
#endif
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
ssl_open(struct url const *urlp, struct sock *sp)
{
   SSL_CTX *ctxp;
   void *confp;
   char const *cp, *cp_base;
   size_t i;
   enum okay rv = STOP;
   NYD_ENTER;

   _ssl_init();

   ssl_set_verify_level(urlp);

   if ((ctxp = SSL_CTX_new(_SSL_CLIENT_METHOD())) == NULL) {
      ssl_gen_err(_("SSL_CTX_new() failed"));
      goto jleave;
   }

   /* Available with OpenSSL 0.9.6 or later */
#ifdef SSL_MODE_AUTO_RETRY
   SSL_CTX_set_mode(ctxp, SSL_MODE_AUTO_RETRY);
#endif

   if ((confp = _ssl_conf_setup(ctxp)) == NULL)
      goto jerr0;

   /* TODO obsolete Check for *ssl-method*, warp to a *ssl-protocol* value */
   if ((cp = xok_vlook(ssl_method, urlp, OXM_ALL)) != NULL) {
      OBSOLETE(_("please use *ssl-protocol* instead of *ssl-method*"));
      if (options & OPT_VERB)
         n_err(_("*ssl-method*: \"%s\"\n"), cp);
      for (i = 0;;) {
         if (!asccasecmp(_ssl_methods[i].sm_name, cp)) {
            cp = _ssl_methods[i].sm_map;
            break;
         }
         if (++i == NELEM(_ssl_methods)) {
            n_err(_("Unsupported TLS/SSL method \"%s\"\n"), cp);
            goto jerr1;
         }
      }
   }
   /* *ssl-protocol* */
   if ((cp_base = xok_vlook(ssl_protocol, urlp, OXM_ALL)) != NULL) {
      if (options & OPT_VERB)
         n_err(_("*ssl-protocol*: \"%s\"\n"), cp_base);
      cp = cp_base;
   }
   cp = (cp != NULL ? savecatsep(cp, ',', SSL_DISABLED_PROTOCOLS)
         : SSL_DISABLED_PROTOCOLS);
   if (!_ssl_conf(confp, SCT_PROTOCOL, cp))
      goto jerr1;

   /* *ssl-cert* */
   if ((cp = xok_vlook(ssl_cert, urlp, OXM_ALL)) != NULL) {
      if (options & OPT_VERB)
         n_err(_("*ssl-cert* \"%s\"\n"), cp);
      if ((cp_base = file_expand(cp)) == NULL) {
         n_err(_("*ssl-cert* value expansion failed: \"%s\"\n"), cp);
         goto jerr1;
      }
      cp = cp_base;
      if (!_ssl_conf(confp, SCT_CERTIFICATE, cp))
         goto jerr1;

      /* *ssl-key* */
      if ((cp_base = xok_vlook(ssl_key, urlp, OXM_ALL)) != NULL) {
         if (options & OPT_VERB)
            n_err(_("*ssl-key* \"%s\"\n"), cp_base);
         if ((cp = file_expand(cp_base)) == NULL) {
            n_err(_("*ssl-key* value expansion failed: \"%s\"\n"), cp_base);
            goto jerr1;
         }
      }
      if (!_ssl_conf(confp, SCT_PRIVATE_KEY, cp))
         goto jerr1;
   }

   if ((cp = xok_vlook(ssl_cipher_list, urlp, OXM_ALL)) != NULL &&
         !_ssl_conf(confp, SCT_CIPHER_STRING, cp))
      goto jerr1;

   if (!_ssl_load_verifications(ctxp))
      goto jerr1;

   if (!_ssl_conf(confp, SCT_OPTIONS, NULL)) /* TODO *ssl-options* */
      goto jerr1;

   /* Done with context setup, create our new per-connection structure */
   if (!_ssl_conf_finish(&confp, FAL0))
      goto jerr0;

   if ((sp->s_ssl = SSL_new(ctxp)) == NULL) {
      ssl_gen_err(_("SSL_new() failed"));
      goto jerr0;
   }

   SSL_set_fd(sp->s_ssl, sp->s_fd);

   if (SSL_connect(sp->s_ssl) < 0) {
      ssl_gen_err(_("could not initiate SSL/TLS connection"));
      goto jerr2;
   }

   if (ssl_verify_level != SSL_VERIFY_IGNORE) {
      if (ssl_check_host(sp, urlp) != OKAY) {
         n_err(_("Host certificate does not match \"%s\"\n"),
            urlp->url_h_p.s);
         if (ssl_verify_decide() != OKAY)
            goto jerr2;
      }
   }

   /* We're fully setup: since we don't reuse the SSL_CTX (pooh) keep it local
    * and free it right now -- it is reference counted by sp->s_ssl.. */
   SSL_CTX_free(ctxp);
   sp->s_use_ssl = 1;
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
jerr2:
   SSL_free(sp->s_ssl);
   sp->s_ssl = NULL;
jerr1:
   if (confp != NULL)
      _ssl_conf_finish(&confp, TRU1);
jerr0:
   SSL_CTX_free(ctxp);
   goto jleave;
}

FL void
ssl_gen_err(char const *fmt, ...)
{
   va_list ap;
   NYD_ENTER;

   va_start(ap, fmt);
   n_verr(fmt, ap);
   va_end(ap);

   n_err(_(": %s\n"), ERR_error_string(ERR_get_error(), NULL));
   NYD_LEAVE;
}

FL int
c_verify(void *vp)
{
   int *msgvec = vp, *ip, ec = 0, rv = 1;
   _STACKOF(X509) *chain = NULL;
   X509_STORE *store = NULL;
   char *ca_dir, *ca_file;
   NYD_ENTER;

   _ssl_init();

   ssl_verify_level = SSL_VERIFY_STRICT;
   if ((store = X509_STORE_new()) == NULL) {
      ssl_gen_err(_("Error creating X509 store"));
      goto jleave;
   }
   X509_STORE_set_verify_cb_func(store, &_ssl_verify_cb);

   if ((ca_dir = ok_vlook(smime_ca_dir)) != NULL)
      ca_dir = file_expand(ca_dir);
   if ((ca_file = ok_vlook(smime_ca_file)) != NULL)
      ca_file = file_expand(ca_file);

   if (ca_dir != NULL || ca_file != NULL) {
      if (X509_STORE_load_locations(store, ca_file, ca_dir) != 1) {
         ssl_gen_err(_("Error loading %s"),
            (ca_file != NULL) ? ca_file : ca_dir);
         goto jleave;
      }
   }
   if (!ok_blook(smime_no_default_ca)) {
      if (X509_STORE_set_default_paths(store) != 1) {
         ssl_gen_err(_("Error loading default CA locations"));
         goto jleave;
      }
   }

   if (load_crls(store, ok_v_smime_crl_file, ok_v_smime_crl_dir) != OKAY)
      goto jleave;

   for (ip = msgvec; *ip != 0; ++ip) {
      struct message *mp = message + *ip - 1;
      setdot(mp);
      ec |= smime_verify(mp, *ip, chain, store);
   }
   if ((rv = ec) != 0)
      exit_status |= EXIT_ERR;
jleave:
   if (store != NULL)
      X509_STORE_free(store);
   NYD_LEAVE;
   return rv;
}

FL FILE *
smime_sign(FILE *ip, char const *addr)
{
   FILE *rv = NULL, *sp = NULL, *fp = NULL, *bp, *hp;
   X509 *cert = NULL;
   _STACKOF(X509) *chain = NULL;
   EVP_PKEY *pkey = NULL;
   BIO *bb, *sb;
   PKCS7 *pkcs7;
   EVP_MD const *md;
   char const *name;
   bool_t bail = FAL0;
   NYD_ENTER;

   _ssl_init();

   if (addr == NULL) {
      n_err(_("No *from* address for signing specified\n"));
      goto jleave;
   }
   if ((fp = smime_sign_cert(addr, NULL, 1)) == NULL)
      goto jleave;

   if ((pkey = PEM_read_PrivateKey(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(_("Error reading private key from"));
      goto jleave;
   }

   rewind(fp);
   if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(_("Error reading signer certificate from"));
      goto jleave;
   }
   Fclose(fp);
   fp = NULL;

   if ((name = _smime_sign_include_certs(addr)) != NULL &&
         !_smime_sign_include_chain_creat(&chain, name))
      goto jleave;

   name = NULL;
   if ((md = _smime_sign_digest(addr, &name)) == NULL)
      goto jleave;

   if ((sp = Ftmp(NULL, "smimesign", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600))
         == NULL) {
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   rewind(ip);
   if (smime_split(ip, &hp, &bp, -1, 0) == STOP)
      goto jerr1;

   sb = NULL;
   pkcs7 = NULL;

   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (sb = BIO_new_fp(sp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO signing objects"));
      bail = TRU1;
      goto jerr;
   }

#undef _X
#define _X  PKCS7_DETACHED | PKCS7_PARTIAL
   if ((pkcs7 = PKCS7_sign(NULL, NULL, chain, bb, _X)) == NULL) {
      ssl_gen_err(_("Error creating the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
   if (PKCS7_sign_add_signer(pkcs7, cert, pkey, md, _X) == NULL) {
      ssl_gen_err(_("Error setting PKCS#7 signing object signer"));
      bail = TRU1;
      goto jerr;
   }
   if (!PKCS7_final(pkcs7, bb, _X)) {
      ssl_gen_err(_("Error finalizing the PKCS#7 signing object"));
      bail = TRU1;
      goto jerr;
   }
#undef _X

   if (PEM_write_bio_PKCS7(sb, pkcs7) == 0) {
      ssl_gen_err(_("Error writing signed S/MIME data"));
      bail = TRU1;
      /*goto jerr*/
   }
jerr:
   if (pkcs7 != NULL)
      PKCS7_free(pkcs7);
   if (sb != NULL)
      BIO_free(sb);
   if (bb != NULL)
      BIO_free(bb);
   if (!bail) {
      rewind(bp);
      fflush_rewind(sp);
      rv = smime_sign_assemble(hp, bp, sp, name);
   } else
jerr1:
      Fclose(sp);

jleave:
   if (chain != NULL)
      sk_X509_pop_free(chain, X509_free);
   if (cert != NULL)
      X509_free(cert);
   if (pkey != NULL)
      EVP_PKEY_free(pkey);
   if (fp != NULL)
      Fclose(fp);
   NYD_LEAVE;
   return rv;
}

FL FILE *
smime_encrypt(FILE *ip, char const *xcertfile, char const *to)
{
   FILE *rv = NULL, *yp, *fp, *bp, *hp;
   X509 *cert;
   PKCS7 *pkcs7;
   BIO *bb, *yb;
   _STACKOF(X509) *certs;
   EVP_CIPHER const *cipher;
   char *certfile;
   bool_t bail = FAL0;
   NYD_ENTER;

   if ((certfile = file_expand(xcertfile)) == NULL)
      goto jleave;

   _ssl_init();

   if ((cipher = _smime_cipher(to)) == NULL)
      goto jleave;
   if ((fp = Fopen(certfile, "r")) == NULL) {
      n_perr(certfile, 0);
      goto jleave;
   }

   if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
      ssl_gen_err(_("Error reading encryption certificate from \"%s\""),
         certfile);
      bail = TRU1;
   }
   Fclose(fp);
   if (bail)
      goto jleave;
   bail = FAL0;

   certs = sk_X509_new_null();
   sk_X509_push(certs, cert);

   if ((yp = Ftmp(NULL, "smimeenc", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      n_perr(_("tempfile"), 0);
      goto jerr1;
   }

   rewind(ip);
   if (smime_split(ip, &hp, &bp, -1, 0) == STOP) {
      Fclose(yp);
      goto jerr1;
   }

   yb = NULL;
   if ((bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL ||
         (yb = BIO_new_fp(yp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO encryption objects"));
      bail = TRU1;
      goto jerr2;
   }
   if ((pkcs7 = PKCS7_encrypt(certs, bb, cipher, 0)) == NULL) {
      ssl_gen_err(_("Error creating the PKCS#7 encryption object"));
      bail = TRU1;
      goto jerr2;
   }
   if (PEM_write_bio_PKCS7(yb, pkcs7) == 0) {
      ssl_gen_err(_("Error writing encrypted S/MIME data"));
      bail = TRU1;
      /* goto jerr2 */
   }

jerr2:
   if (bb != NULL)
      BIO_free(bb);
   if (yb != NULL)
      BIO_free(yb);
   Fclose(bp);
   if (bail)
      Fclose(yp);
   else {
      fflush_rewind(yp);
      rv = smime_encrypt_assemble(hp, yp);
   }
jerr1:
   sk_X509_pop_free(certs, X509_free);
jleave:
   NYD_LEAVE;
   return rv;
}

FL struct message *
smime_decrypt(struct message *m, char const *to, char const *cc, int signcall)
{
   struct message *rv;
   FILE *fp, *bp, *hp, *op;
   X509 *cert;
   PKCS7 *pkcs7;
   EVP_PKEY *pkey;
   BIO *bb, *pb, *ob;
   long size;
   FILE *yp;
   NYD_ENTER;

   rv = NULL;
   cert = NULL;
   pkey = NULL;
   size = m->m_size;

   if ((yp = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;

   _ssl_init();

   if ((fp = smime_sign_cert(to, cc, 0)) != NULL) {
      pkey = PEM_read_PrivateKey(fp, NULL, &ssl_password_cb, NULL);
      if (pkey == NULL) {
         ssl_gen_err(_("Error reading private key"));
         Fclose(fp);
         goto jleave;
      }
      rewind(fp);

      if ((cert = PEM_read_X509(fp, NULL, &ssl_password_cb, NULL)) == NULL) {
         ssl_gen_err(_("Error reading decryption certificate"));
         Fclose(fp);
         EVP_PKEY_free(pkey);
         goto jleave;
      }
      Fclose(fp);
   }

   if ((op = Ftmp(NULL, "smimedec", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600)) ==
         NULL) {
      n_perr(_("tempfile"), 0);
      goto j_ferr;
   }

   if (smime_split(yp, &hp, &bp, size, 1) == STOP)
      goto jferr;

   if ((ob = BIO_new_fp(op, BIO_NOCLOSE)) == NULL ||
         (bb = BIO_new_fp(bp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err(_("Error creating BIO decryption objects"));
      goto jferr;
   }
   if ((pkcs7 = SMIME_read_PKCS7(bb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object"));
jferr:
      Fclose(op);
j_ferr:
      if (cert)
         X509_free(cert);
      if (pkey)
         EVP_PKEY_free(pkey);
      goto jleave;
   }

   if (PKCS7_type_is_signed(pkcs7)) {
      if (signcall) {
         setinput(&mb, m, NEED_BODY);
         rv = (struct message*)-1;
         goto jerr2;
      }
      if (PKCS7_verify(pkcs7, NULL, NULL, NULL, ob,
            PKCS7_NOVERIFY | PKCS7_NOSIGS) != 1)
         goto jerr;
      fseek(hp, 0L, SEEK_END);
      fprintf(hp, "X-Encryption-Cipher: none\n");
      fflush(hp);
      rewind(hp);
   } else if (pkey == NULL) {
      n_err(_("No appropriate private key found\n"));
      goto jerr2;
   } else if (cert == NULL) {
      n_err(_("No appropriate certificate found\n"));
      goto jerr2;
   } else if (PKCS7_decrypt(pkcs7, pkey, cert, ob, 0) != 1) {
jerr:
      ssl_gen_err(_("Error decrypting PKCS#7 object"));
jerr2:
      BIO_free(bb);
      BIO_free(ob);
      Fclose(op);
      Fclose(bp);
      Fclose(hp);
      if (cert != NULL)
         X509_free(cert);
      if (pkey != NULL)
         EVP_PKEY_free(pkey);
      goto jleave;
   }
   BIO_free(bb);
   BIO_free(ob);
   if (cert)
      X509_free(cert);
   if (pkey)
      EVP_PKEY_free(pkey);
   fflush_rewind(op);
   Fclose(bp);

   rv = smime_decrypt_assemble(m, hp, op);
jleave:
   NYD_LEAVE;
   return rv;
}

FL enum okay
smime_certsave(struct message *m, int n, FILE *op)
{
   struct message *x;
   char *to, *cc, *cnttype;
   int c, i;
   FILE *fp, *ip;
   off_t size;
   BIO *fb, *pb;
   PKCS7 *pkcs7;
   _STACKOF(X509) *certs, *chain = NULL;
   X509 *cert;
   enum okay rv = STOP;
   NYD_ENTER;

   _ssl_msgno = (size_t)n;
jloop:
   to = hfield1("to", m);
   cc = hfield1("cc", m);
   cnttype = hfield1("content-type", m);

   if ((ip = setinput(&mb, m, NEED_BODY)) == NULL)
      goto jleave;

#undef _X
#undef _Y
#define _X     (sizeof("application/") -1)
#define _Y(X)  X, sizeof(X) -1
   if (cnttype && is_asccaseprefix(cnttype, "application/") &&
         (!ascncasecmp(cnttype + _X, _Y("pkcs7-mime")) ||
          !ascncasecmp(cnttype + _X, _Y("x-pkcs7-mime")))) {
#undef _Y
#undef _X
      if ((x = smime_decrypt(m, to, cc, 1)) == NULL)
         goto jleave;
      if (x != (struct message*)-1) {
         m = x;
         goto jloop;
      }
   }
   size = m->m_size;

   if ((fp = Ftmp(NULL, "smimecert", OF_RDWR | OF_UNLINK | OF_REGISTER, 0600))
         == NULL) {
      n_perr(_("tempfile"), 0);
      goto jleave;
   }

   while (size-- > 0) {
      c = getc(ip);
      putc(c, fp);
   }
   fflush(fp);

   rewind(fp);
   if ((fb = BIO_new_fp(fp, BIO_NOCLOSE)) == NULL) {
      ssl_gen_err("Error creating BIO object for message %d", n);
      Fclose(fp);
      goto jleave;
   }

   if ((pkcs7 = SMIME_read_PKCS7(fb, &pb)) == NULL) {
      ssl_gen_err(_("Error reading PKCS#7 object for message %d"), n);
      BIO_free(fb);
      Fclose(fp);
      goto jleave;
   }
   BIO_free(fb);
   Fclose(fp);

   certs = PKCS7_get0_signers(pkcs7, chain, 0);
   if (certs == NULL) {
      n_err(_("No certificates found in message %d\n"), n);
      goto jleave;
   }

   for (i = 0; i < sk_X509_num(certs); ++i) {
      cert = sk_X509_value(certs, i);
      if (X509_print_fp(op, cert) == 0 || PEM_write_X509(op, cert) == 0) {
         ssl_gen_err(_("Error writing certificate %d from message %d"),
            i, n);
         goto jleave;
      }
   }
   rv = OKAY;
jleave:
   NYD_LEAVE;
   return rv;
}
#endif /* HAVE_OPENSSL */

/* s-it-mode */
