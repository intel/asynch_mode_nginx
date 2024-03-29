#!/usr/bin/perl

# Copyright (C) Intel, Inc.
# (C) Maxim Dounin

# Tests for nginx realip module.

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

my $t = Test::Nginx->new()->has(qw/http realip rewrite/);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    add_header X-IP $remote_addr;
    set_real_ip_from  127.0.0.1/32;
    set_real_ip_from  10.0.1.0/24;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location / { }
        location /custom {
            real_ip_header    X-Real-IP-Custom;
        }

        location /1 {
            real_ip_header    X-Forwarded-For;
            real_ip_recursive off;
        }

        location /2 {
            real_ip_header    X-Forwarded-For;
            real_ip_recursive on;
        }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  localhost;

        return 204;
    }
}

EOF

$t->write_file('index.html', '');
$t->write_file('custom', '');
$t->write_file('1', '');
$t->write_file('2', '');
$t->run();

plan(skip_all => 'no 127.0.0.1 on host')
	if http_get('/') !~ /X-IP: 127.0.0.1/m;

$t->plan(8);

###############################################################################

like(http(<<EOF), qr/^X-IP: 192.0.2.1/m, 'realip');
GET / HTTP/1.0
Host: localhost
X-Real-IP: 192.0.2.1

EOF

like(http(<<EOF), qr/^X-IP: 192.0.2.1/m, 'realip custom');
GET /custom HTTP/1.0
Host: localhost
X-Real-IP-Custom: 192.0.2.1

EOF

like(http_xff('/1', '10.0.0.1, 192.0.2.1'), qr/^X-IP: 192.0.2.1/m,
	'realip multi');
like(http_xff('/1', '192.0.2.1, 10.0.1.1, 127.0.0.1'),
	qr/^X-IP: 127.0.0.1/m, 'realip recursive off');
like(http_xff('/2', '10.0.1.1, 192.0.2.1, 127.0.0.1'),
	qr/^X-IP: 192.0.2.1/m, 'realip recursive on');

like(http(<<EOF), qr/^X-IP: 10.0.1.1/m, 'realip multi xff recursive off');
GET /1 HTTP/1.0
Host: localhost
X-Forwarded-For: 192.0.2.1
X-Forwarded-For: 127.0.0.1, 10.0.1.1

EOF

like(http(<<EOF), qr/^X-IP: 192.0.2.1/m, 'realip multi xff recursive on');
GET /2 HTTP/1.0
Host: localhost
X-Forwarded-For: 10.0.1.1
X-Forwarded-For: 192.0.2.1
X-Forwarded-For: 127.0.0.1

EOF

my $s = IO::Socket::INET->new('127.0.0.1:' . port(8081));
like(http(<<EOF, socket => $s), qr/ 204 .*192.0.2.1/s, 'realip post read');
GET / HTTP/1.0
Host: localhost
X-Real-IP: 192.0.2.1

EOF

###############################################################################

sub http_xff {
	my ($uri, $xff) = @_;
	return http(<<EOF);
GET $uri HTTP/1.0
Host: localhost
X-Forwarded-For: $xff

EOF
}

###############################################################################
