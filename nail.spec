# Sccsid @(#)nail.spec	1.23 (gritter) 6/13/04
Summary: A MIME capable implementation of the mailx command
Name: nail
Version: 10.8
Release: 1
License: BSD
Group: Applications/Internet
Source: %{name}-%{version}.tar.bz2
URL: <http://nail.sourceforge.net>
Vendor: Gunnar Ritter <Gunnar.Ritter@pluto.uni-freiburg.de>
Packager: Didar Hussain <dhs@rediffmail.com>
BuildRoot: %{_tmppath}/%{name}-root

%description
Nail is derived from Berkeley Mail and is intended provide the 
functionality of the POSIX.2 mailx command with additional support
for MIME messages, POP3 and SMTP.

Install nail if you need a command line tool with the ability to
handle MIME messages.

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
# Some RedHat releases refuse to compile with OpenSSL unless
# -I/usr/kerberos/include is given.
test -d /usr/kerberos/include &&INCLUDES=-I/usr/kerberos/include export INCLUDES
rm -rf %{buildroot}
%setup

%build
test -d /usr/kerberos/include &&INCLUDES=-I/usr/kerberos/include export INCLUDES
make %{makeflags}

%install
test -d /usr/kerberos/include &&INCLUDES=-I/usr/kerberos/include export INCLUDES
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
