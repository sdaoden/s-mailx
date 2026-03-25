#!/usr/bin/env python3
#@ Create and update OAuth2 access tokens (for S-nail).
#@
#@ Lots of help and input from Stephen Isard, thank you!
#
# 2022 - 2026 Steffen Nurpmeso <steffen@sdaoden.eu>
# Public Domain

SELF = 'oauth-helper.py'
VERSION = '0.8.3'
CONTACT = 'Steffen Nurpmeso <steffen@sdaoden.eu>'

# Empty and no builtin configs
VAL_NAME = 'S-nail'

import argparse
import base64
from datetime import datetime as dati
import hashlib
import http.server
import json
import os
import pickle
import secrets
import socket
import subprocess
import sys
import time
from urllib.error import HTTPError
from urllib.parse import urlencode, urlparse, parse_qs
from urllib.request import urlopen

EX_OK = 0
EX_USAGE = 64
EX_DATAERR= 65
EX_NOINPUT = 66
EX_SOFTWARE = 70
EX_CANTCREAT = 73
EX_TEMPFAIL = 75

# (default)
DEVICECODE_GRANT_TYPE = 'urn:ietf:params:oauth:grant-type:device_code'

# Note: we use .keys() for configuration checks: all providers need _all_ keys.
providers = { #{{{
	'Google': {
		'authorize_endpoint': 'https://accounts.google.com/o/oauth2/auth',
		'devicecode_endpoint': 'https://oauth2.googleapis.com/device/code',
		'devicecode_grant_type': None,
		'token_endpoint': 'https://accounts.google.com/o/oauth2/token',
		'redirect_uri': 'urn:ietf:wg:oauth:2.0:oob',
		'scope': 'https://mail.google.com/',
		'flow': 'redirect',
		# Provider hacks
		'access_type': None,
		'flow_redirect_uri_port_fixed': None,
		'refresh_needs_authorize': None,
		'scope_fixed': None,
		'tenant': None
	},
	'Microsoft': {
		'authorize_endpoint': 'https://login.microsoftonline.com/common/oauth2/v2.0/authorize',
		'devicecode_endpoint': 'https://login.microsoftonline.com/common/oauth2/v2.0/devicecode',
		'devicecode_grant_type': None,
		'token_endpoint': 'https://login.microsoftonline.com/common/oauth2/v2.0/token',
		'redirect_uri': 'https://login.microsoftonline.com/common/oauth2/nativeclient',
		'scope': (
				'offline_access https://outlook.office.com/IMAP.AccessAsUser.All '
				'https://outlook.office.com/POP.AccessAsUser.All '
				'https://outlook.office.com/SMTP.Send'
			),
		'flow': 'redirect',
		# Provider hacks
		'access_type': None,
		'flow_redirect_uri_port_fixed': None,
		'refresh_needs_authorize': None,
		'scope_fixed': 'y',
		'tenant': 'common'
	},
	'Yandex': {
		'authorize_endpoint': 'https://oauth.yandex.com/authorize',
		'devicecode_endpoint': 'https://oauth.yandex.com/device/code',
		'devicecode_grant_type': 'device_code',
		'token_endpoint': 'https://oauth.yandex.com/token',
		'redirect_uri': 'https://oauth.yandex.com/verification_code',
		'scope': 'mail:imap_full mail:imap_ro mail:smtp',
		'flow': 'redirect',
		# Provider hacks
		'access_type': None,
		'flow_redirect_uri_port_fixed': 33333,
		'refresh_needs_authorize': None,
		'scope_fixed': None,
		'tenant': None
	},
	'Zoho': {
		'authorize_endpoint': 'https://accounts.zoho.com/oauth/v2/auth',
		'devicecode_endpoint': 'https://accounts.zoho.com/oauth/v2/token',
		'devicecode_grant_type': None,
		'token_endpoint': 'https://accounts.zoho.com/oauth/v2/token',
		'redirect_uri': 'http://localhost',
		'scope': (
			'ZohoMail.accounts.READ '
			'ZohoMail.folders.CREATE ZohoMail.folders.READ ZohoMail.folders.UPDATE ZohoMail.folders.DELETE '
			'ZohoMail.messages.CREATE ZohoMail.messages.READ '
				'ZohoMail.messages.UPDATE ZohoMail.messages.DELETE'
			),
		'flow': 'redirect',
		# Provider hacks
		'access_type': 'offline',
		'flow_redirect_uri_port_fixed': 'port_number_to_use',
		'refresh_needs_authorize': None,
		'scope_fixed': None,
		'tenant': None
	}
}
#}}}

# Note: we use .keys() and '' for configuration checks!
client = {
	'access_token': '',
	'client_id': '',
	'client_secret': None, # optional
	'refresh_token': '', # effectively optional
	'login_hint': None # optional
}

def arg_parser(): #{{{
	p = argparse.ArgumentParser(
			description=SELF + ' (' + VERSION +
					'): Manage RFC 6749 OAuth 2.0 Authorization Framework access tokens',
			epilog='''
Create a new --resource for --provider via --action=template, fill in
client_id=, maybe login_hint= (contains documentation!), and all other
provider needs (also see according --provider specific --action=manual).
Run --action=access; at least the first run really is --action=authorize!
Force an access token --action=update even for non-expired timeouts.
(For S-nail one might get away with --action=access --provider=X --resource=Y.)''' +
'  Bugs/Contact via ' + CONTACT)

	p.add_argument('-A', '--automatic', action='store_true',
		help='no interactivity, exit error if that would happen')
	p.add_argument('-a', '--action', dest='action',
		choices=('access', 'authorize', 'manual', 'template', 'update'), default='access',
		help='action to perform'),
	p.add_argument('-H', '--hook', dest='hook', metavar='X', default=None, help='''
a configuration load/save hook script may be used; invoked via "load" or "save"
plus given --resource argument, data format is the same. (Note: values not quoted!)
		'''),
	p.add_argument('-p', '--provider', dest='provider', choices=providers, default=None,
		help='Technology Giant; ignored if --resource yet exists!')
	p.add_argument('-R', '--resource', required=True, dest='resource', metavar='X',
		help='configuration file')
	p.add_argument('-d', '--debug', action='store_true', help='be noisy')

	return p
#}}}

def config_load(args, dt): #{{{
	if args.debug:
		print('# Try load resource %s' % args.resource, file=sys.stderr)

	#
	if args.hook:
		try:
			sub = subprocess.check_output([args.hook, 'load', args.resource])
			cfg = dict((s.strip().decode() for s in l.split(b'=', 1))
					for l in sub.split(b'\n')
						if (not l.strip().startswith(b'#') and l.find(b'=') != -1))
			return config_check(args, cfg, dt)
		except Exception as e:
			print('PANIC: loading --resource via --hook failed: %s: %s: %s' %
				(args.resource, args.hook, e), file=sys.stderr)
			return EX_NOINPUT

	#
	try:
		s = os.stat(args.resource).st_mode
		if s & 0o0177:
			s &= 0o0777
			print('! Warning: --resource mode permissions other than '
				'user read/write: 0%o: %s' % (s, args.resource), file=sys.stderr)

		with open(args.resource) as f:
			cfg = dict((s.strip() for s in l.split('=', 1))
					for l in f if (not l.strip().startswith('#') and l.find('=') != -1))
		if args.debug:
			print('# Loaded config: %s' % cfg, file=sys.stderr)
		return config_check(args, cfg, dt)
	except FileNotFoundError:
		pass
	except Exception as e:
		print('PANIC: config load: %s' % e, file=sys.stderr)
		return EX_NOINPUT

	#
	if not VAL_NAME:
		print('PANIC: no configuration provided, please try --help', file=sys.stderr)
		return EX_USAGE
	if args.debug:
		print('# Using built-in resource', file=sys.stderr)
	p = args.provider
	if not p:
		print('PANIC: --provider argument is initially needed', file=sys.stderr)
		return EX_USAGE

	exec(compile('mi=' + str(pickle.loads(base64.standard_b64decode(bfgw))), '/dev/null', 'exec'), globals())
	if not mi.get(p):
		print('PANIC: there is no built-in configuration for provider %s' % p, file=sys.stderr)
		print('PANIC: please try --action=manual --provider=%s' % p, file=sys.stderr)
		return EX_NOINPUT

	cfg = providers[p]
	for k in mi[p].keys():
		cfg[k] = mi[p][k]
	if args.debug:
		print('# Config: %s' % cfg, file=sys.stderr)

	if args.hook:
		return cfg

	if args.debug:
		print('# Auto-creating --resource %s for --provider %s' % (args.resource, p), file=sys.stderr)
	su = os.umask(0o0077)
	try:
		with open(args.resource, 'x') as f:
			f.write('# ' + args.resource + ', written ' + str(dt) + '\n')
			config_save_head(f, args)
			for k in cfg.keys():
				if cfg[k]:
					f.write(k + ' = ' + cfg[k] + '\n')
	except FileExistsError:
		print('PANIC: --resource must not exist: %s' % args.resource, file=sys.stderr)
		return EX_USAGE
	except Exception as e:
		print('PANIC: --resource creation failed: %s' % e, file=sys.stderr)
		return EX_CANTCREAT
	print('. Auto-created --resource %s for --provider %s' % (args.resource, p), file=sys.stderr)

	return cfg
#}}}

def config_check(args, cfg, dt): #{{{
	p = args.provider
	if not p:
		p = 'Google'
	p = providers[p]
	e = False
	for k in p.keys():
		if not cfg.get(k) and p.get(k):
			print('PANIC: missing service key: %s' % k, file=sys.stderr)
			e = True
	if not cfg.get('client_id'):
		print('PANIC: missing "client_id"', file=sys.stderr)
		e = True
	if e:
		return EX_DATAERR
	return cfg
#}}}

def config_save(args, cfg, dt): #{{{
	if args.debug:
		print('# Writing resource %s' % args.resource, file=sys.stderr)
	if cfg.get('timeout'):
		cfg['timestamp'] = str(int(dt.timestamp()))
	else:
		cfg['timestamp'] = cfg['timeout'] = None

	if args.hook:
		try:
			i = ''
			for k in cfg.keys():
				if cfg.get(k):
					i = i + k + '=' + cfg[k] + '\n'
			i = i.encode()
			subprocess.run([args.hook, 'save', args.resource], check=True, input=i)
			return EX_OK
		except Exception as e:
			print('PANIC: saving --resource via --hook failed: %s: %s: %s' % (args.resource, args.hook, e),
				file=sys.stderr)
			return EX_CANTCREAT

	#
	try:
		with open(args.resource + '.new', 'w') as f:
			f.write('# ' + args.resource + ', written ' + str(dt) + '\n')
			#config_save_head(f, args)
			for k in cfg.keys():
				if cfg.get(k):
					f.write(k + '=' + cfg[k] + '\n')
		os.rename(args.resource + '.new', args.resource)
		return EX_OK
	except Exception as e:
		print('PANIC: saving --resource failed: %s: %s' % (args.resource, e), file=sys.stderr)
		return EX_CANTCREAT
#}}}

def config_save_head(f, args): #{{{
	f.write('# Syntax of this resource file:\n')
	f.write('# . Lines beginning with "#" are comments and ignored\n')
	f.write('# . Empty lines are ignored\n')
	f.write('# . Other lines must adhere to "KEY = VALUE" syntax,\n')
	f.write('#   line continuation over multiple lines is not supported\n')
	f.write('# . Leading/trailing whitespace of lines, KEY, VALUE is erased\n')
	f.write('#\n')
	f.write('# If not given by a built-in provider support, the following\n')
	f.write('# fields have to be [optionally/provider dependent] filled in:\n')
	f.write('#\n')
	f.write('# . client_id= the OAuth 2.0 client_id= of the application\n')
	f.write('#\n')
	f.write('# . [client_secret= the client secret of the application]\n')
	f.write('#\n')
	f.write('# . [devicecode_grant_type= grant type for flow=redirect]\n')
	f.write('#   The default is ' + DEVICECODE_GRANT_TYPE + '\n')
	f.write('#\n')
	f.write('# . flow= auth | devicecode | redirect\n')
	f.write('#   All flows require the user to open an URL that is shown,\n')
	f.write('#   and follow the instructions of the used provider in the browser window.\n')
	f.write('#   Javascript capability is a requirement?!\n')
	f.write('#   - auth: browser goes web, user oks + copy+paste a token.\n')
	f.write('#     The token is usually shown as a HTML document, but some providers\n')
	f.write('#     only redirect the browser to an empty document, the token is then part\n')
	f.write('#     of the URL, then: http://BLA?code=TOKEN\n')
	f.write('#   - devicecode: browser goes web, user oks, script polls web periodically\n')
	f.write('#   - redirect: browser goes web, user oks, browser redirects to localhost URL.\n')
	f.write('#     Note: requires that browser and script run on the same box, and even\n')
	f.write('#     in the same namespace/container/sandbox, because the script temporarily\n')
	f.write('#     acts as a HTTP server, and must be reachable!\n')
	f.write('#\n')
	f.write('# . [login_hint= email-address; multi-account support convenience]\n')
	f.write('#\n')
	f.write('# . [scope= resources the application shall access at provider]\n')
	f.write('#\n')
	f.write('# Provider hacks:\n')
	f.write('# Unfortunately one to multiple of the following "hacks" may be required:\n')
	f.write('#\n')
	f.write('# . access_type= desired access type (online, offline)\n')
	f.write('#\n')
	f.write('# . flow_redirect_uri_port_fixed= fixed port number to use\n')
	f.write('#   For flow=redirect providers may match redirect_uri against a complete URL\n')
	f.write('#   including port number, for example, "http://localhost:33333".\n')
	f.write('#   If set the HTTP server uses this port exclusively: it MUST be accessible\n')
	f.write('#   from the outside, and it MUST be accessible by normal users\n')
	f.write('#\n')
	f.write('# . scope_fixed= any value; do not update scope= from provider responsese\n')
	f.write('#\n')
	f.write('# . tenant= directory tenant of the application\n')
	f.write('#\n')
	f.write('# . refresh_needs_authorize= any value: always authorize\n')
	f.write('#   A provider may impolitely forbid RFC 6749, 6. Refreshing an Access Token,\n')
	f.write('#   but always require RFC 6749, 4.1.1. Authorization Request\n')

	if VAL_NAME:
		f.write('#\n')
		f.write('# NOTE: prefilled application-specific of the above refer to ' + VAL_NAME + '\n')
#}}}

def response_check_and_config_save(args, cfg, dt, resp): #{{{
	# RFC 6749, 5.1.  Successful Response
	if not resp.get('access_token'): # or not resp.get('refresh_token'):
		print('PANIC: response did not provide required access_token: %s' % resp, file=sys.stderr)
		return EX_TEMPFAIL

	# REQUIRED
	cfg['access_token'] = resp['access_token']
	#token_type

	# RECOMMENDET
	if resp.get('expires_in'):
		try:
			i = int(resp['expires_in'])
			if i < 0:
				i = 666
		except Exception as e:
			print('! Ignoring invalid "expires_in" response: %s: %s' % (e, resp['expires_in']), file=sys.stderr)
			i = 3000
		cfg['timeout'] = str(i)
	else:
		cfg['timeout'] = None

	# OPTIONAL
	if resp.get('refresh_token'):
		cfg['refresh_token'] = resp.get('refresh_token')
	if not cfg.get('scope_fixed') and resp.get('scope'):
		cfg['scope'] = resp.get('scope')

	print('%s' % cfg['access_token'])
	return config_save(args, cfg, dt)
#}}}

def act_template(args, dt): #{{{
	p = args.provider
	if not p:
		print('PANIC: --provider argument is initially needed', file=sys.stderr)
		return EX_USAGE

	if args.debug:
		print('# Creating template --resource %s' % args.resource, file=sys.stderr)
	su = os.umask(0o0077)
	try:
		xp = providers[p]
		if VAL_NAME:
			exec(compile('mi=' + str(pickle.loads(base64.standard_b64decode(bfgw))), '/dev/null', 'exec'), globals())
			if mi.get(p):
				for k in mi[p].keys():
					v = mi[p][k]
					if k in client.keys():
						client[k] = v
					else:
						xp[k] = v

		with open(args.resource, 'x') as f:
			f.write('# ' + args.resource + ', written ' + str(dt) + '\n')
			config_save_head(f, args)
			f.write('\n# Service keys (for provider=' + args.provider + ')\n')
			for k in xp.keys():
				v = ''
				if xp.get(k):
					v = xp[k]
				f.write(k + '=' + v + '\n')
			f.write('\n# Client keys\n')
			for k in client.keys():
				if client.get(k):
					v = ' ' + client[k]
				else:
					v = ''
				f.write(k + '=' + v + '\n')
	except FileExistsError:
		print('PANIC: --resource must not exist: %s' % args.resource, file=sys.stderr)
		return EX_USAGE
	except Exception as e:
		print('PANIC: --resource creation failed: %s: %s' % (args.resource, e), file=sys.stderr)
		return EX_CANTCREAT

	print('. Created --resource file: %s' % args.resource, file=sys.stderr)
	return EX_OK
#}}}

auth_code = None
def act_authorize(args, cfg, dt): #{{{
	if args.automatic:
		return EX_NOINPUT
	if not sys.stdin.isatty():
		print('! Standard input is not a terminal; as we need input now, this is unsupported',
			file=sys.stderr)
		return EX_USAGE

	global auth_code
	print('* OAuth 2.0 RFC 6749, 4.1.1. Authorization Request', file=sys.stderr)
	e = False
	for k in client.keys():
		if k != 'refresh_token' and k != 'access_token' and not cfg.get(k) and client.get(k, '.') == '':
			print('! Missing client key: %s' % k, file=sys.stderr)
			e = True
	if e:
		print('PANIC: configuration incomplete or invalid', file=sys.stderr)
		return EX_DATAERR

	p = {}
	p['response_type'] = 'code'
	p['client_id'] = cfg['client_id']
	redir = cfg.get('redirect_uri')
	if redir:
		p['redirect_uri'] = redir
	if cfg.get('scope'):
		p['scope'] = cfg['scope']
	# Not according to RFC, but pass if available
	#if cfg.get('client_secret'):
	#	p['client_secret'] = cfg['client_secret']
	if cfg.get('tenant'):
		p['tenant'] = cfg['tenant']
	if cfg.get('access_type'):
		p['access_type'] = cfg['access_type']
	# XXX add a 'state', and re-check that
	if cfg.get('login_hint'):
		p['login_hint'] = cfg['login_hint']

	print('  . To create an authorization code, please visit the shown URL:\n', file=sys.stderr)

	b = os.getenv('BROWSER')

	if cfg['flow'] == 'devicecode':
		return act__authorize_devicecode(args, cfg, dt, b, p)

	# Add RFC 7636 "Proof Key for Code Exchange by OAuth Public Clients"
	cod_ver = secrets.token_urlsafe(84)
	p['code_challenge'] = base64.urlsafe_b64encode(hashlib.sha256(cod_ver.encode()).digest())[:-1]
	p['code_challenge_method'] = 'S256'

	if not cfg.get('flow') or cfg['flow'] == 'auth':
		p = urlencode(p)
		u = cfg['authorize_endpoint'] + '?' + p
		if b:
			b = b + " '" + u + "'"
			print(b, file=sys.stderr)
			print('\n    - Shall i invoke this command? [y/else] ', end='', file=sys.stderr)
			try:
				i = input()
			except KeyboardInterrupt:
				print('PANIC: interrupt', file=sys.stderr)
				return EX_TEMPFAIL
			if i == 'Y' or i == 'y':
				os.system(b)
		else:
			print('   %s' % u, file=sys.stderr)

		print('\nPlease enter authorization [URI?code=]token (empty: exit): ', end='', file=sys.stderr)
		try:
			auth_code = input()
		except KeyboardInterrupt:
			print('PANIC: interrupt', file=sys.stderr)
			return EX_TEMPFAIL
	elif cfg['flow'] == 'redirect':
		try:
			s = socket.socket()
			i = 0
			if cfg.get('flow_redirect_uri_port_fixed'):
				i = int(cfg['flow_redirect_uri_port_fixed'])
			s.bind(('127.0.0.1', i))
		except Exception as e:
			print('PANIC: impossible to create/bind socket, try again later: %s' % e, file=sys.stderr)
			return EX_TEMPFAIL
		(sa, sp) = s.getsockname()
		sa = 'localhost' # XXX hm
		s.close()

		p['redirect_uri'] = redir = 'http://' + sa + ':' + str(sp) + '/'
		p = urlencode(p)
		if args.debug:
			print('# URL is %s' % p, file=sys.stderr)
		u = cfg['authorize_endpoint'] + '?' + p
		if b:
			print("   %s '%s'" % (b, u), file=sys.stderr)
		else:
			print('   %s' % u, file=sys.stderr)
		print('   [..waiting for browser to come back via redirect..\n    ..to %s (local :PORT number)..]' % redir,
			file=sys.stderr, flush=True)

		class django(http.server.BaseHTTPRequestHandler):
			def __init__(self, request, client_address, server):
				self.django_rv = 200
				super().__init__(request, client_address, server)
			def do_HEAD(self):
				self.send_response(self.django_rv)
				self.send_header('Content-type', 'text/html')
				self.end_headers()
			def do_GET(self):
				global auth_code
				qs = urlparse(self.path).query
				qd = parse_qs(qs)
				if qd.get('code'):
					auth_code = qd['code'][0]
					self.do_HEAD()
#		 b'<p><small>Response was ' + str(qd).encode('ascii') + b'</small></p>'
					self.wfile.write(b'<html><head><title>Authorized</title></head>'
						b'<body onload="self.close()"><h1>Authorization successful</h1>'
						b'<p><strong>This window can be closed</strong></p>'
						b'</body></html>')
				else:
					self.django_rv = 400
					self.do_HEAD()
#		 b'<p><small>Response was ' + str(qd).encode('ascii') + b'</small></p>'
					self.wfile.write(b'<html><head><title>Not authorized</title></head><body">'
						b'<h1>Authorization NOT successful!</h1>'
						b'<p><strong>This window can be closed</strong></p>'
						b'</body></html>')
					auth_code = EX_NOINPUT
			def log_request(self, code='-', size='-'):
				pass

		with http.server.HTTPServer((sa, sp), django) as httpd:
			try:
				httpd.handle_request()
			except Exception as e:
				print('PANIC: HTTP server handle: %s' % e, file=sys.stderr)
				return EX_NOINPUT

	else:
		print('PANIC: IMPLERR', file=sys.stderr)
		return EX_SOFTWARE

	if not auth_code:
		print('PANIC: could not obtain an authorization code', file=sys.stderr)
		return EX_NOINPUT
	if not isinstance(auth_code, str):
		return auth_code

	#
	print('\n* OAuth 2.0 RFC 6749, 4.1.3. Access Token Request', file=sys.stderr)
	p = {}
	p['grant_type'] = 'authorization_code'
	p['client_id'] = cfg['client_id']
	p['code'] = auth_code
	p['code_verifier'] = cod_ver
	if redir:
		p['redirect_uri'] = redir
	# Not according to RFC, but pass if available
	if cfg.get('client_secret'):
		p['client_secret'] = cfg['client_secret']
	if cfg.get('scope'):
		p['scope'] = cfg['scope']
	if cfg.get('tenant'):
		p['tenant'] = cfg['tenant']
	if cfg.get('access_type'):
		p['access_type'] = cfg['access_type']
	p = urlencode(p).encode('ascii')
	if args.debug:
		print('# URL is %s' % p, file=sys.stderr)

	try:
		resp = json.loads(urlopen(cfg['token_endpoint'], p).read())
		if args.debug:
			print('# Response is %s' % resp, file=sys.stderr)
	except Exception as e:
		print('PANIC: access token response: %s' % e, file=sys.stderr)
		return EX_NOINPUT
	return response_check_and_config_save(args, cfg, dt, resp)
#}}}

def act__authorize_devicecode(args, cfg, dt, b, p): #{{{
	p = urlencode(p).encode('ascii')
	if args.debug:
		print('# URL is %s' % p, file=sys.stderr)
	try:
		resp = urlopen(cfg['devicecode_endpoint'], p)
	except HTTPError as e:
		resp = e
	except Exception as e:
		print('PANIC: devicecode response: %s' % e, file=sys.stderr)
		return EX_NOINPUT
	resp = resp.read()
	if args.debug:
		print('# Response is %s' % resp, file=sys.stderr)
	resp = json.loads(resp)

	if resp.get('error'):
		print('PANIC: devicecode error response: %s' % resp, file=sys.stderr)
		return EX_NOINPUT

	# Yandex: verification_url
	if not resp.get('verification_uri') and resp.get('verification_url'):
		resp['verification_uri'] = resp['verification_url']
	if not resp.get('device_code') or not resp.get('user_code') or not resp.get('verification_uri'):
		print('PANIC: incomplete devicecode response: %s', resp, file=sys.stderr)
		return EX_NOINPUT

	if resp.get('message'):
		print('Server said: %s\n' % resp['message'], file=sys.stderr)
	else:
		print('Server expects user to input code: %s\n' % resp['user_code'], file=sys.stderr)
	if b:
		print("    %s '%s'" % (b, resp['verification_uri']), file=sys.stderr)
	else:
		print('    %s' % resp['verification_uri'], file=sys.stderr)

	p = {}
	if cfg.get('devicecode_grant_type'):
		p['grant_type'] = cfg['devicecode_grant_type']
	else:
		p['grant_type'] = DEVICECODE_GRANT_TYPE
	# Yandex: code; just set both!
	p['device_code'] = p['code'] = resp['device_code']
	p['client_id'] = cfg['client_id']
	# Not according to RFC, but pass if available
	if cfg.get('client_secret'):
		p['client_secret'] = cfg['client_secret']
	if cfg.get('tenant'):
		p['tenant'] = cfg['tenant']
	if cfg.get('access_type'):
		p['access_type'] = cfg['access_type']
	p = urlencode(p).encode('ascii')

	ival = int(resp.get('interval', '5'))
	if ival > 20:
		ival = 20
	print('\n  . Polling server each %s seconds for grant: ' % ival, end='', flush=True, file=sys.stderr)
	sep = ''
	while True:
		time.sleep(ival)
		if args.debug:
			print('[POLL]', end='', flush=True, file=sys.stderr)
		try:
			resp = urlopen(cfg['token_endpoint'], p)
		except HTTPError as e:
			resp = e
		except Exception as e:
			print('%sfail\nPANIC: devicecode poll: %s' % (sep, e), file=sys.stderr)
			return EX_NOINPUT
		resp = json.loads(resp.read())
		if not resp.get('error'):
			print('%sok' % sep, file=sys.stderr)
			return response_check_and_config_save(args, cfg, dt, resp)
		if resp['error'] != 'authorization_pending':
			break
		print('.', end='', flush=True, file=sys.stderr)
		sep = ' '

	if resp['error'] == 'authorization_declined':
		print('%sdeclined by user' % sep, file=sys.stderr)
		return EX_NOINPUT
	if resp['error'] == 'bad_verification_code':
		print('%sfail\nPANIC: bad verification code, please rerun script' % sep, file=sys.stderr)
		return EX_TEMPFAIL
	if resp['error'] == 'expired_token':
		print('%sfail\nPANIC: token timeout expired, please rerun script' % sep, file=sys.stderr)
		return EX_TEMPFAIL
	print('%sfail\nPANIC: implementation error, unknown error: %s' % (sep, resp), file=sys.stderr)
	return EX_SOFTWARE
#}}}

def act_access(args, cfg, dt): #{{{
	if args.debug or args.action != 'access':
		print('* OAuth 2.0 RFC 6749, 6.  Refreshing an Access Token', file=sys.stderr)
	e = False
	for k in client.keys():
		if not cfg.get(k) and client.get(k, '.') == '':
			print('! Missing client key: %s' % k, file=sys.stderr)
			e = True
	if e:
		print('  ! Configuration incomplete; need --authorize', file=sys.stderr)
		return act_authorize(args, cfg, dt)

	if cfg.get('refresh_needs_authorize'):
		if args.debug:
			print('# Configuration enforces refresh_needs_authorize\n', file=sys.stderr)
		return act_authorize(args, cfg, dt)

	p = {}
	p['grant_type'] = 'refresh_token'
	p['client_id'] = cfg['client_id']
	if not cfg.get('refresh_token'):
		print('PANIC: IMPLERR', file=sys.stderr)
		return EX_SOFTWARE
	p['refresh_token'] = cfg['refresh_token']
	if cfg.get('scope'):
		p['scope'] = cfg['scope']
	# Not according to RFC, but pass if available
	if cfg.get('client_secret'):
		p['client_secret'] = cfg['client_secret']
	if cfg.get('tenant'):
		p['tenant'] = cfg['tenant']
	if cfg.get('access_type'):
		p['access_type'] = cfg['access_type']
	p = urlencode(p).encode('ascii')
	if args.debug:
		print('# URL is %s' % p, file=sys.stderr)

	try:
		resp = json.loads(urlopen(cfg['token_endpoint'], p).read())
		if args.debug:
			print('# Response is %s' % resp, file=sys.stderr)
	except KeyboardInterrupt:
		print('PANIC: interrupt', file=sys.stderr)
		return EX_TEMPFAIL
	except Exception as e:
		print('  ! refresh_token response: %s' % e, file=sys.stderr)
		if args.automatic:
			return EX_NOINPUT
		print('  ! Let us try --authorize instead (sleeping 3 seconds)', file=sys.stderr)
		try:
			time.sleep(3)
		except KeyboardInterrupt:
			print('PANIC: interrupt', file=sys.stderr)
			return EX_TEMPFAIL
		return act_authorize(args, cfg, dt)
	return response_check_and_config_save(args, cfg, dt, resp)
#}}}

def main(): #{{{
	args = arg_parser().parse_args()

	if args.action == 'manual':
		return act_manual(args) # Below because it is long text

	dt = dati.today()

	if args.action == 'template':
		return act_template(args, dt)

	cfg = config_load(args, dt)
	if not isinstance(cfg, dict):
		return cfg

	if not cfg.get('access_token') or args.action == 'authorize':
		return act_authorize(args, cfg, dt)
	elif args.action == 'access' and cfg.get('timeout') and cfg.get('timestamp'):
		try:
			i = int(cfg['timestamp'])
			i = int(dt.timestamp()) - i + 600
			j = int(cfg['timeout'])
			if i < j:
				if args.debug:
					print('# Timeout not yet reached (%s/%s secs/mins to go)' %
						((j - i), ((j - i) / 60)), file=sys.stderr)
				print('%s' % cfg['access_token'])
				return EX_OK
		except Exception as e:
			print('! Configuration with invalid timestamp/timeout: %s' % e, file=sys.stderr)
			cfg['timeout'] = cfg['timestamp'] = None
	else:
		cfg['timeout'] = cfg['timestamp'] = None
	return act_access(args, cfg, dt)
#}}}

def act_manual(args): #{{{
	global VAL_NAME

	# (In alphabetical order)
	if not args.provider:
		print('! Manual for which --provider?')
		print('A more generic documentation is placed in a generated --resource')
		print('Eg, just dump a resource for whatever provider, then edit')
		return EX_USAGE
	elif args.provider == 'Google':
		print('''[From mutt.org, contrib/mutt_oauth2.py.README,
by Alexander Perlis, 2020-07-15

-- How to create a Google registration --

Go to console.developers.google.com, and create a new project. The name doesn't
matter and could be "mutt registration project".

 - Go to Library, choose Gmail API, and enable it
 - Hit left arrow icon to get back to console.developers.google.com
 - Choose OAuth Consent Screen
	- Choose Internal for an organizational G Suite
	- Choose External if that's your only choice
	- For Application Name, put for example "Mutt"
	- Under scopes, choose Add scope, scroll all the way down, enable the
	  "https://mail.google.com/" scope
[Note this only allow "internal" users; you get the same mail usage scope
by selecting those gmail scopes without any lock symbol!
Like this application verification is not needed, and "External" can be
chosen.]
	- Fill out additional fields (application logo, etc) if you feel like it
	  (will make the consent screen look nicer)
[- Add yourself to "Test users"]
 - Back at console.developers.google.com, choose Credentials
 - At top, choose Create Credentials / OAuth2 client iD
	- Application type is "Desktop app"
]
For Google we need a client_id= and a client_secret=.
As of 2026-03-16 authorization requests require flow=redirect.
			''')
	elif args.provider == 'Microsoft':
		print('''[From mutt.org, contrib/mutt_oauth2.py.README,
by Alexander Perlis, 2020-07-15

-- How to create a Microsoft registration --

Go to portal.azure.com, log in with a Microsoft account (get a free
one at outlook.com), then search for "app registration", and add a
new registration. On the initial form that appears, put a name like
"Mutt", allow any type of account, and put "http://localhost/" as
the redirect URI, then more carefully go through each
screen:

Branding
 - Leave fields blank or put in reasonable values
 - For official registration, verify your choice of publisher domain
Authentication:
 - Platform "Mobile and desktop"
 - Redirect URI https://login.microsoftonline.com/common/oauth2/nativeclient
[as via --action=template; add http://localhost in addition!]
 - Any kind of account
 - Enable public client (allow device code flow)
API permissions:
 - Microsoft Graph, Delegated, "offline_access"
 - Microsoft Graph, Delegated, "IMAP.AccessAsUser.All"
 - Microsoft Graph, Delegated, "POP.AccessAsUser.All"
 - Microsoft Graph, Delegated, "SMTP.Send"
 - Microsoft Graph, Delegated, "User.Read"
Overview:
 - Take note of the Application ID (a.k.a. Client ID), you'll need it shortly
[and the tenant=]

End users who aren't able to get to the app registration screen within
portal.azure.com for their work/school account can temporarily use an
incognito browser window to create a free outlook.com account and use that
to create the app registration.
]
For Microsoft we need a client_id=, and (optionally?) a tenant=.
Thanks to Ian Collier of Oxford University on mutt-dev@ one solution
for problems may be to say tenant=common instead of using the tenant ID.
Thanks to Stephen Isard for the scope_fixed hack!
			''')
	elif args.provider == 'Yandex':
		print('''
-- How to create a Yandex registration --

Yandex has a clear, clean and logical documentation at oauth.yandex.com.
Note that for flow=redirect you need to add the http://localhost:PORT
URL (PORT best outside the "user-inaccessible" port numbers 0-1024).
Configure flow_redirect_uri_port_fixed=PORT and
devicecode_grant_type=device_code.
			''')
	elif args.provider == 'Zoho':
		print('''
Go to https://www.zoho.com/mail/help/api/using-oauth-2.html and register
a "non-browser mobile application" with a redirect http://localhost:PORT
URL (PORT best outside the "user-inaccessible" port numbers 0-1024).
Configure set flow_redirect_uri_port_fixed=PORT.
			''')
		VAL_NAME = None
	else:
		VAL_NAME = None

	if VAL_NAME:
		print('For %s we have a built-in configuration for this provider' % VAL_NAME)

	print('Run "--action=template --provider=%s", and fill it in.' % args.provider)
	print('Finally run "--action=authorize" with --resource to go.')
	return EX_OK
if VAL_NAME:
	bfgw = (b'gASVaAEAAAAAAAB9lCiMBkdvb2dsZZR9lCiMCWNsaWVudF9pZJSMSDE2NzMyMjcw'
			b'NjAyMi1rc2dzbXEyYmtsZzYzaGFuY3FxMjF1bG52amk3azdobC5hcHBzLmdvb2ds'
			b'ZXVzZXJjb250ZW50LmNvbZSMDWNsaWVudF9zZWNyZXSUjCNHT0NTUFgtbFRING54'
			b'Y2V6QmR5YXJxekthTXdIWGRaOW5zbZR1jAlNaWNyb3NvZnSUfZQoaAOMJGJmMGY0'
			b'NDg4LTA4OWUtNDZlZS1hNDhkLThmMDcxNzM4OGJlM5SMBnRlbmFudJSMBmNvbW1v'
			b'bpR1jAZZYW5kZXiUfZQoaAOMIDRkMWQ5OTYxM2UwYzRmMWFhZDcyNTBiNjI4Zjc3'
			b'YjljlGgFjCAzZGRlOWFkOTRiMmU0NWJiYjQwM2RkOTRiM2ExY2Y2OZSMHGZsb3df'
			b'cmVkaXJlY3RfdXJpX3BvcnRfZml4ZWSUjAUzMzMzM5R1dS4=')
#}}}

if __name__ == '__main__':
  sys.exit(main())

# s-itt-mode
