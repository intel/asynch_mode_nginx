#!/bin/bash

#***************************************************************************
# Copyright (C) Intel, Inc
#
# To perform nginx tests we need following package installed:
#     libxslt libxslt-devel gd-devel perl pcre-devel GeoIP.x86_64 GeoIP-devel
#***************************************************************************

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=`dirname "$SCRIPT"`
IGNORE_LIST="proxy_ssl_conf_command.t uwsgi_ssl_certificate.t uwsgi_ssl_certificate_vars.t ssl_stapling.t ssl_ocsp.t access_log.t access_log_variables.t addition.t  h2_proxy_ssl.t h2_ssl_proxy_cache.t h2_ssl_proxy_protocol.t h2_ssl_variables.t h2_ssl_verify_client.t proxy_ssl.t proxy_ssl.t proxy_if.t proxy_ssl_certificate_vars.t proxy_ssl_conf_command.t proxy_ssl_verify.t proxy_ssl_certificate.t proxy_ssl_name.t ssl.t ssl_certificate.t ssl_certificates.t ssl_certificate_perl.t ssl_conf_command.t ssl_password_file.t ssl_curve.t ssl_reject_handshake.t ssl_sni.t ssl_sni_sessions.t ssl_verify_client.t mail_ssl_session_reuse.t stream_proxy_ssl_certificate_vars.t stream_proxy_ssl_conf_command.t stream_ssl_alpn.t stream_ssl_conf_command.t stream_ssl_preread_alpn.t proxy_bind_transparent.t stream_upstream_zone_ssl.t stream_ssl_realip.t stream_ssl_variables.t stream_ssl_verify_client.t ssl_session_reuse.t addition_buffered.t ssl_session_ticket_key.t stream_pass.t stream_server_name.t stream_ssl_reject_handshake.t stream_ssl_session_reuse.t upstream_zone_ssl.t uwsgi_ssl.t uwsgi_ssl_verify.t"

if [ ! -d "$NGINX_INSTALL_DIR" ]; then
    echo -e "NGINX_INSTALL_DIR not set. Run:\n\t export NGINX_INSTALL_DIR=<asynch_mode_nginx installation directory>\n"
    exit 0
fi

if [ ! -d "$OPENSSL_ROOT" ]; then
    echo -e "OPENSSL_ROOT not set. Run:\n\t export OPENSSL_ROOT=<openssl source code directory>\n"
    exit 0
fi

if [ ! -d "$OPENSSL_LIB" ]; then
    echo -e "OPENSSL_LIB not set. Run:\n\t export OPENSSL_LIB=<openssl installation directory>\n"
    exit 0
fi

if [ ! -d "$OPENSSL_ENGINES" ]; then
    echo -e "OPENSSL_ENGINES not set. Run:\n\t export OPENSSL_ENGINES=<openssl engine installation directory>\n"
    exit 0
fi

if [ ! -d "$NGINX_SRC_DIR" ]; then
    echo -e "NGINX_SRC_DIR not set. Run:\n\t export NGINX_SRC_DIR=<asynch_mode_nginx source code directory>\n"
    exit 0
fi

if [ ! -d "$QZ_ROOT" ]; then
    echo -e "QZ_ROOT not set. Run:\n\t export QZ_ROOT=<QATzip source code directory>\n"
    exit 0
fi

function printHelp ()
{
    echo -e "Usage ./nginx-test.sh X [case name]\n" "X can be:\n" "\t qat -- qatengine\n" "\t dasync -- openssl async engine\n" "\t official -- original nginx-tests"
}

if [ "$#" == '1' ];then
    if [[ $1 != 'qat' && $1 != 'dasync' && $1 != 'official' ]];then
        printHelp
        exit 0
    fi
elif [ "$#" == '2' ];then
    echo "----------- specify test case --------------"
    echo $2
else
    printHelp
    exit 0
fi

if test "`grep "define OPENSSL_VERSION_MAJOR" $OPENSSL_LIB/include/openssl/opensslv.h | awk '{print $4}'`" = "3"
then
    echo "Run official test against OpenSSL 3.0"
    openssl_lib=$OPENSSL_LIB/lib64
else
    echo "Run official test against OpenSSL 1.1.1"
    openssl_lib=$OPENSSL_LIB/lib
fi

cd $NGINX_SRC_DIR
./configure \
--prefix=$NGINX_INSTALL_DIR \
--user=root \
--group=root \
--with-file-aio \
--with-http_realip_module \
--with-http_addition_module \
--with-http_xslt_module \
--with-http_image_filter_module \
--with-http_geoip_module \
--with-http_sub_module \
--with-http_dav_module \
--with-http_flv_module \
--with-http_mp4_module \
--with-http_gzip_static_module \
--with-http_random_index_module \
--with-http_secure_link_module \
--with-http_degradation_module \
--with-http_stub_status_module \
--with-http_perl_module \
--with-http_auth_request_module \
--with-mail \
--with-mail_ssl_module \
--with-debug \
--with-http_gunzip_module \
--with-http_ssl_module \
--with-http_v2_module \
--with-http_slice_module \
--with-stream \
--with-stream_ssl_module \
--with-stream_ssl_preread_module \
--with-stream_realip_module \
--add-dynamic-module=$NGINX_SRC_DIR/modules/nginx_qat_module \
--add-dynamic-module=$NGINX_SRC_DIR/modules/nginx_qatzip_module \
--with-cc-opt="-DNGX_SECURE_MEM -DNGX_INTEL_SDL -I$OPENSSL_LIB/include -I$ICP_ROOT/quickassist/include -I$ICP_ROOT/quickassist/include/dc -I$QZ_ROOT/include -Wno-error=deprecated-declarations" \
--with-ld-opt="-Wl,-rpath=$openssl_lib:$QZ_ROOT/src/.libs -L$openssl_lib -L$QZ_ROOT/src/.libs -lqatzip -lz"

make -j$(nproc) && make install

#Only for Centos 7, prepare env..."

NGINX_PERL_OBJS=$NGINX_SRC_DIR/objs/src/http/modules/perl
NGINX_AUTO_OBJS=$NGINX_PERL_OBJS/blib/arch/auto/nginx
NGINX_PERL_INSTALL_DIR=/usr/share/perl5/
NGINX_AUTO_INSTALL_DIR=/usr/share/perl5/auto/nginx/
mkdir -p $NGINX_AUTO_INSTALL_DIR
cp $NGINX_PERL_OBJS/nginx.bs $NGINX_PERL_INSTALL_DIR
cp $NGINX_AUTO_OBJS/nginx.so $NGINX_AUTO_INSTALL_DIR
cp $NGINX_PERL_OBJS/nginx.pm $NGINX_PERL_INSTALL_DIR
cp $OPENSSL_ROOT/engines/dasync.so $OPENSSL_ENGINES/
cp $NGINX_SRC_DIR/objs/ngx_ssl_engine_qat_module.so $NGINX_INSTALL_DIR/modules/ngx_ssl_engine_qat_module_for_test.so;
cp $NGINX_SRC_DIR/objs/ngx_http_qatzip_filter_module.so $NGINX_INSTALL_DIR/modules/ngx_http_qatzip_filter_module_for_test.so
cp objs/nginx $NGINX_INSTALL_DIR/sbin/nginx-for-test;

#Prepare envionment variables...

if [ $1 == 'qat' ];then
    # Enable async against QAT_Engine
    export TEST_LOAD_NGINX_MODULE="load_module $NGINX_INSTALL_DIR/modules/ngx_ssl_engine_qat_module_for_test.so;"
    export TEST_LOAD_QATZIP_MODULE="load_module $NGINX_INSTALL_DIR/modules/ngx_http_qatzip_filter_module_for_test.so;"
    export TEST_NGINX_GLOBALS="
    $TEST_LOAD_NGINX_MODULE
    $TEST_LOAD_QATZIP_MODULE
    ssl_engine {
        use_engine qatengine;
        default_algorithms ALL;
        qat_engine {
            qat_offload_mode async;
            qat_notify_mode poll;
            qat_poll_mode heuristic;
        }
    }
    "
    export TEST_NGINX_SYNC_GLOBALS="
    $TEST_LOAD_NGINX_MODULE
    $TEST_LOAD_QATZIP_MODULE
    ssl_engine {
        use_engine qatengine;
        default_algorithms ALL;
        qat_engine {
            qat_offload_mode sync;
            qat_notify_mode poll;
        }
    }
    "
    export TEST_NGINX_GLOBALS_HTTP="qatzip_sw only;"
    export TEST_NGINX_GLOBALS_HTTPS="ssl_asynch on;"
    export GRPC_ASYNCH_ENABLE="grpc_ssl_asynch on;"
    export PROXY_ASYNCH_ENABLE="proxy_ssl_asynch on;"
    export PROXY_ASYNCH_DISABLE="proxy_ssl_asynch off;"
    export SSL_ASYNCH=" asynch"
    export GZIP_MIN_LENGTH_0="gzip_min_length 0;"
    export QATZIP_TYPES="qatzip_types text/plain;"
    export QATZIP_ENABLE="qatzip_sw no;"
    export QATZIP_DISABLE="qatzip_sw only;"
    export QATZIP_MIN_LENGTH_0="qatzip_min_length 0;"
elif [ $1 == 'dasync' ];then
    # Enable async against dasync engine
    export TEST_NGINX_GLOBALS="
    ssl_engine {
        use_engine dasync;
    }
    "
    export TEST_NGINX_GLOBALS_HTTPS="ssl_asynch on;"
    export GRPC_ASYNCH_ENABLE="grpc_ssl_asynch on;"
    export PROXY_ASYNCH_ENABLE="proxy_ssl_asynch on;"
    export PROXY_ASYNCH_DISABLE="proxy_ssl_asynch off;"
    export SSL_ASYNCH=" asynch"
    export GZIP_TYPES="gzip_types text/plain;"
    export GZIP_MIN_LENGTH_0="gzip_min_length 0;"
elif [ $1 == 'official' ]; then
    # Original nginx tesets
    export GZIP_TYPES="gzip_types text/plain;"
    export GZIP_MIN_LENGTH_0="gzip_min_length 0;"
fi

# Start to platform tests...

SELF=$$
: > $SCRIPTPATH/nginx-test.log

if [ $2 ]
then
    CASES=`find  $NGINX_SRC_DIR/test/nginx-tests/ -name $2`
else
    CASES=`find  $NGINX_SRC_DIR/test/nginx-tests/ -name *.t`
fi

for CASE in $CASES
do
    echo "case $CASE start to run" >> $SCRIPTPATH/nginx-test.log

    # Ignore the case if it's in the ignore list
    CASE_NAME=`basename $CASE`
    if [[ $IGNORE_LIST =~ (^|[[:space:]])"$CASE_NAME"($|[[:space:]]) ]]
    then
        echo "case $CASE .. ignored" >> $SCRIPTPATH/nginx-test.log
        continue
    fi

    TEST_NGINX_BINARY=$NGINX_INSTALL_DIR/sbin/nginx-for-test prove $CASE >> $SCRIPTPATH/nginx-test.log 2>&1 &
    CASEPID=$!
    ( sleep 60; kill -9 $CASEPID > /dev/null 2>&1 && echo "case $CASE failed" && echo -e "Nginx Official Test RESULT:FAIL" && kill -9 $SELF ) &
    DOG=$!
    DOGPPID=$PPID
    wait $CASEPID
    kill $DOGPPID
    RESULT=`grep Failed $SCRIPTPATH/nginx-test.log`
    if [ "$RESULT" != "" ]
    then
        echo "case $CASE failed"
        echo -e "Nginx Official Test RESULT:FAIL"
        exit 0
    fi
done

echo -e "Nginx Official Test RESULT:PASS"

