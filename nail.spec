# Sccsid @(#)nail.spec	1.13 (gritter) 11/15/03
Summary: A MIME capable implementation of the mailx command
Name: nail
Version: 10.6
Release: 1
License: BSD
Group: Applications/Internet
Source: %{name}-%{version}.tar.gz
URL: <http://omnibus.ruf.uni-freiburg.de/~gritter/>
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
rm -rf %{buildroot}
%setup
%configure --prefix=/usr --with-openssl

%build
make

%install
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
