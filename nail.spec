# Sccsid @(#)nail.spec	1.16 (gritter) 3/19/04
Summary: A MIME capable implementation of the mailx command
Name: nail
Version: 10.7
Release: 1
License: BSD
Group: Applications/Internet
Source: %{name}-%{version}.tar.bz2
URL: <http://nail.berlios.de>
Vendor: Gunnar Ritter <Gunnar.Ritter@pluto.uni-freiburg.de>
Packager: Didar Hussain <dhs@rediffmail.com>
BuildRoot: %{_tmppath}/%{name}-root

%description
Nail is derived from Berkeley Mail and is intended provide the 
functionality of the POSIX.2 mailx command with additional support
for MIME messages, POP3 and SMTP.

Install nail if you need a command line tool with the ability to
handle MIME messages.

%prep
INCLUDES=-I/usr/kerberos/include export INCLUDES
rm -rf %{buildroot}
%setup
%configure --prefix=/usr --with-openssl

%build
INCLUDES=-I/usr/kerberos/include export INCLUDES
make

%install
INCLUDES=-I/usr/kerberos/include export INCLUDES
make DESTDIR=%{buildroot} install
gzip -9 %{buildroot}/usr/share/man/man1/nail.1

%clean
cd ..; rm -rf %{_builddir}/%{name}-%{version}
rm -rf %{buildroot}

%files
%doc COPYING AUTHORS INSTALL README TODO I18N ChangeLog
%config(noreplace) /etc/nail.rc
/usr/bin/nail
/usr/share/man/man1/nail*
