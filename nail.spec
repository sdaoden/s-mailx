# Sccsid @(#)nail.spec	1.29 (gritter) 9/20/04

%define	use_nss	0
%define	mozilla_version	1.6

Summary: An enhanced implementation of the mailx command
Name: nail
Version: 11.13
Release: 1
License: BSD
Group: Applications/Internet
Source: %{name}-%{version}.tar.bz2
URL: <http://nail.sourceforge.net>
Vendor: Gunnar Ritter <Gunnar.Ritter@pluto.uni-freiburg.de>
Packager: Didar Hussain <dhs@rediffmail.com>
BuildRoot: %{_tmppath}/%{name}-root

%if %{use_nss}
Requires: mozilla-nss
Requires: mozilla-nspr
BuildRequires: mozilla-nss-devel
BuildRequires: mozilla-nspr-devel
BuildRequires: /usr/include/mozilla-%{mozilla_version}/nss/cms.h
%endif

%description
Nail is derived from Berkeley Mail and is intended provide the 
functionality of the POSIX mailx command with additional support
for MIME messages, IMAP, POP3, and SMTP. It provides enhanced
features for interactive use, such as caching and disconnected
operation for IMAP, message threading, scoring, and filtering.
It is also usable as a mail batch language, both for sending
and receiving mail.

%define	prefix		/usr
%define	bindir		%{prefix}/bin
%define	mandir		%{prefix}/share/man
%define	sysconfdir	/etc
%define	mailrc		%{sysconfdir}/nail.rc
%define	mailspool	/var/mail
%define	sendmail	/usr/lib/sendmail
%define	ucbinstall	install
%define	cflags		-O2 -fomit-frame-pointer

%define	makeflags	PREFIX=%{prefix} BINDIR=%{bindir} MANDIR=%{mandir} SYSCONFDIR=%{sysconfdir} MAILRC=%{mailrc} MAILSPOOL=%{mailspool} SENDMAIL=%{sendmail} UCBINSTALL=%{ucbinstall} CFLAGS="%{cflags}"

%prep
%setup

%build
rm -rf %{buildroot}

# Some RedHat releases refuse to compile with OpenSSL unless
# -I/usr/kerberos/include is given. To compile with GSSAPI authentication
# included, they also need -L/usr/kerberos/lib.
test -d /usr/kerberos/include &&
	INCLUDES="$INCLUDES -I/usr/kerberos/include" export INCLUDES
test -d /usr/kerberos/lib &&
	LDFLAGS="$LDFLAGS -L/usr/kerberos/lib" export LDFLAGS

%if %{use_nss}
INCLUDES="$INCLUDES -I/usr/include/mozilla-%{mozilla_version}/nspr"
INCLUDES="$INCLUDES -I/usr/include/mozilla-%{mozilla_version}/nss"
export INCLUDES
%endif

make %{makeflags}

%install
make DESTDIR=%{buildroot} %{makeflags} install
gzip -9 %{buildroot}/usr/share/man/man1/nail.1

%clean
cd ..; rm -rf %{_builddir}/%{name}-%{version}
rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc COPYING AUTHORS INSTALL README TODO I18N ChangeLog
%config(noreplace) /etc/nail.rc
/usr/bin/nail
/usr/share/man/man1/nail*
