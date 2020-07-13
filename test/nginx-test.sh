#!/bin/bash

# Copyright (C) Intel, Inc
#For the nginx test,the packages installed by yum
#   cpan hg pcre geoip perl-Geo-IP.x86_64
#   GeoIP-devel.x86_64 memcached perl-FCGI.x86_64
#   perl-GDGraph.noarch perl-ExtUtils-Embed.noarch
#
#For the nginx test,the perl packages installed
#   Test::Nginx Protocol::WebSocket
#   IO::Socket::SSL Cache::Memcached
#   Cache::Memcached::Fast

# Function Timeout().
# Avoiding the nginx tests timeout and stuck.
Timeout()
{
    waitfor=3600
    TEST_NGINX_BINARY=$NGINX_INSTALL_DIR/sbin/nginx-for-test prove . >$SCRIPTPATH/nginx-test.log &
    commandpid=$!

    ( sleep $waitfor ; kill -9 $commandpid  > /dev/null 2>&1 ) &
    watchdog=$!
    sleeppid=$PPID

    wait $commandpid > /dev/null 2>&1
    kill $sleeppid > /dev/null 2>&1
    return 0
}

#pre-set the env
SCRIPT=$(readlink -f "$0")
SCRIPTPATH=`dirname "$SCRIPT"`

# compile and install the nginx via the config.test
cd $SCRIPTPATH;
killall nginx;
cp ./nginx-config.test $NGINX_SRC_DIR/;
cd $NGINX_SRC_DIR/;
chmod +x ./nginx-config.test ;
./nginx-config.test;
make;
cp objs/nginx $NGINX_INSTALL_DIR/sbin/nginx-for-test;

cd $NGINX_SRC_DIR
cp objs/ngx_ssl_engine_qat_module.so $NGINX_INSTALL_DIR/modules/ngx_ssl_engine_qat_module_for_test.so;
cp objs/ngx_http_qatzip_filter_module.so $NGINX_INSTALL_DIR/modules/ngx_http_qatzip_filter_module_for_test.so

cd $SCRIPTPATH;
#Configure according to the nginx test type
if [ "$#" == '1' ];then
  if [ $1 == 'qat' ];then
    #Update async nginx config file to running with QATZip and QATEngine.
    export TEST_NGINX_MODULES=$NGINX_INSTALL_DIR/modules
    export TEST_LOAD_NGINX_MODULE="load_module $TEST_NGINX_MODULES/ngx_ssl_engine_qat_module_for_test.so;"
    export TEST_LOAD_QATZIP_MODULE="load_module $TEST_NGINX_MODULES/ngx_http_qatzip_filter_module_for_test.so;"
    export TEST_NGINX_GLOBALS="
    $TEST_LOAD_NGINX_MODULE
    $TEST_LOAD_QATZIP_MODULE
    ssl_engine {
        use_engine qat;
        default_algorithms ALL;
        qat_engine {
            qat_offload_mode async;
            qat_notify_mode poll;
            qat_poll_mode heuristic;
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
    #Update async nginx config file to running without QATZip and QATEngine.
    export TEST_NGINX_MODULES=$NGINX_INSTALL_DIR/modules
    export TEST_NGINX_GLOBALS="
    ssl_engine {
        use_engine dasync;
    }
    "
    export GZIP_TYPES="gzip_types text/plain;"
    export GZIP_MIN_LENGTH_0="gzip_min_length 0;"
  else
    echo "The parameter qat or dasync needs to be passed in, read the README.md for more information."
    exit
  fi
else
    echo "The parameter qat or dasync needs to be passed in, read the README.md for more information."
    exit
fi

cd ./nginx-tests

#run the test
Timeout ;
cd $SCRIPTPATH;
RESULT1=`tail -1  $SCRIPTPATH/nginx-test.log | grep "PASS" | wc -l`
if (( $RESULT1 ))
then
        echo -e "Nginx Official Test RESULT:PASS"
else
        echo -e "Nginx Official Test RESULT:FAIL"
fi

#clean up the env
killall nginx-for-test
exit 0;

