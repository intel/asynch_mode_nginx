#!/usr/bin/perl

# Copyright (C) Intel, Inc.
# (C) Sergey Kandaurov
# (C) Nginx, Inc.

# Tests for http ssl module, ssl_crl directive.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http http_ssl socket_ssl/)
	->has_daemon('openssl')->plan(3);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    ssl_certificate_key  localhost.key;
    ssl_certificate localhost.crt;

    ssl_verify_client on;
    ssl_client_certificate int-root.crt;

    add_header X-Verify $ssl_client_verify always;

    server {
        listen       127.0.0.1:8080 ssl;
        server_name  localhost;
        %%TEST_NGINX_GLOBALS_HTTPS%%

        ssl_client_certificate root.crt;
        ssl_crl empty.crl;
    }

    server {
        listen       127.0.0.1:8081 ssl;
        server_name  localhost;
        %%TEST_NGINX_GLOBALS_HTTPS%%

        ssl_client_certificate root.crt;
        ssl_crl root.crl;
    }

    server {
        listen       127.0.0.1:8082 ssl;
        server_name  localhost;
        %%TEST_NGINX_GLOBALS_HTTPS%%

        ssl_verify_depth 2;
        ssl_crl root.crl;
    }
}

EOF

my $d = $t->testdir();

$t->write_file('openssl.conf', <<EOF);
[ req ]
default_bits = 2048
encrypt_key = no
distinguished_name = req_distinguished_name
x509_extensions = myca_extensions
[ req_distinguished_name ]
[ myca_extensions ]
basicConstraints = critical,CA:TRUE
EOF

$t->write_file('ca.conf', <<EOF);
[ ca ]
default_ca = myca

[ myca ]
new_certs_dir = $d
database = $d/certindex
default_md = sha256
policy = myca_policy
serial = $d/certserial
default_days = 1

[ myca_policy ]
commonName = supplied
EOF

foreach my $name ('root', 'localhost') {
	system('openssl req -x509 -new '
		. "-config $d/openssl.conf -subj /CN=$name/ "
		. "-out $d/$name.crt -keyout $d/$name.key "
		. ">>$d/openssl.out 2>&1") == 0
		or die "Can't create certificate for $name: $!\n";
}

foreach my $name ('int', 'end') {
	system("openssl req -new "
		. "-config $d/openssl.conf -subj /CN=$name/ "
		. "-out $d/$name.csr -keyout $d/$name.key "
		. ">>$d/openssl.out 2>&1") == 0
		or die "Can't create certificate for $name: $!\n";
}

$t->write_file('certserial', '1000');
$t->write_file('certindex', '');

system("openssl ca -batch -config $d/ca.conf "
	. "-keyfile $d/root.key -cert $d/root.crt "
	. "-subj /CN=int/ -in $d/int.csr -out $d/int.crt "
	. ">>$d/openssl.out 2>&1") == 0
	or die "Can't sign certificate for int: $!\n";

system("openssl ca -batch -config $d/ca.conf "
	. "-keyfile $d/int.key -cert $d/int.crt "
	. "-subj /CN=end/ -in $d/end.csr -out $d/end.crt "
	. ">>$d/openssl.out 2>&1") == 0
	or die "Can't sign certificate for end: $!\n";

system("openssl ca -gencrl -config $d/ca.conf "
	. "-keyfile $d/root.key -cert $d/root.crt "
	. "-out $d/empty.crl -crldays 1 "
	. ">>$d/openssl.out 2>&1") == 0
	or die "Can't create empty crl: $!\n";

system("openssl ca -config $d/ca.conf -revoke $d/int.crt "
	. "-keyfile $d/root.key -cert $d/root.crt "
	. ">>$d/openssl.out 2>&1") == 0
	or die "Can't revoke int.crt: $!\n";

system("openssl ca -gencrl -config $d/ca.conf "
	. "-keyfile $d/root.key -cert $d/root.crt "
	. "-out $d/root.crl -crldays 1 "
	. ">>$d/openssl.out 2>&1") == 0
	or die "Can't update crl: $!\n";

$t->write_file('int-root.crt',
	$t->read_file('int.crt') . $t->read_file('root.crt'));

$t->write_file('t', '');
$t->run();

###############################################################################

like(get(8080, 'int'), qr/SUCCESS/, 'crl - no revoked certs');
like(get(8081, 'int'), qr/FAILED/, 'crl - client cert revoked');
like(get(8082, 'end'), qr/FAILED/, 'crl - intermediate cert revoked');

###############################################################################

sub get {
	my ($port, $cert) = @_;
	http_get(
		'/t', PeerAddr => '127.0.0.1:' . port($port),
		SSL => 1,
		SSL_cert_file => "$d/$cert.crt",
		SSL_key_file => "$d/$cert.key"
	);
}

###############################################################################
