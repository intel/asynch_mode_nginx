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

# the function is for avoiding the nginx timeout
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
    sed -i 's/use_engine dasync.*/use_engine qat;/' test-env.sh;
    sed -i 's/#default_algorithms ALL.*/default_algorithms ALL;/' test-env.sh;
    sed -i 's/#qat_engine.*/qat_engine {/' test-env.sh;
    sed -i 's/#qat_offload_mode async.*/qat_offload_mode async;/' test-env.sh;
    sed -i 's/#qat_notify_mode poll.*/qat_notify_mode poll;/' test-env.sh;
    sed -i 's/#qat_poll_mode heuristic;}.*/qat_poll_mode heuristic;}/' test-env.sh;
    sed -i 's/export TEST_LOAD_NGINX_MODULE.*/export TEST_LOAD_NGINX_MODULE="load_module $TEST_NGINX_MODULES\/ngx_ssl_engine_qat_module_for_test.so;"/' test-env.sh;
    sed -i 's/export TEST_LOAD_QATZIP_MODULE.*/export TEST_LOAD_QATZIP_MODULE="load_module $TEST_NGINX_MODULES\/ngx_http_qatzip_filter_module_for_test.so;"/' test-env.sh;
    sed -i 's/export TEST_NGINX_GLOBALS_HTTPS.*/export TEST_NGINX_GLOBALS_HTTPS="ssl_asynch on;"/' test-env.sh;
    source ./test-env.sh
    export GZIP_ENABLE=""
    export GZIP_MIN_LENGTH_0=""
  elif [ $1 == 'dasync' ];then
    cp $OPENSSL_SRC_DIR/engines/dasync.so $OPENSSL_LIB/lib/engines-1.1
    sed -i 's/use_engine qat.*/use_engine dasync;/' test-env.sh;
    sed -i 's/.*default_algorithms ALL.*/#default_algorithms ALL;/' test-env.sh;
    sed -i 's/.*qat_engine.*/#qat_engine {/' test-env.sh;
    sed -i 's/.*qat_offload_mode async.*/#qat_offload_mode async;/' test-env.sh;
    sed -i 's/.*qat_notify_mode poll.*/#qat_notify_mode poll;/' test-env.sh;
    sed -i 's/.*qat_poll_mode heuristic;}.*/#qat_poll_mode heuristic;}/' test-env.sh;
    sed -i 's/export TEST_LOAD_NGINX_MODULE.*/export TEST_LOAD_NGINX_MODULE=""/' test-env.sh;
    sed -i 's/export TEST_LOAD_QATZIP_MODULE.*/export TEST_LOAD_QATZIP_MODULE="load_module $TEST_NGINX_MODULES\/ngx_http_qatzip_filter_module_for_test.so;"/' test-env.sh;
    sed -i 's/export TEST_NGINX_GLOBALS_HTTPS.*/export TEST_NGINX_GLOBALS_HTTPS=""/' test-env.sh;
    source ./test-env.sh
    export QATZIP_ENABLE=""
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
        echo -e "Nginx Test RESULT:PASS"
else
        echo -e "Nginx Test RESULT:FAIL"
fi

#clean up the env
killall nginx-for-test
exit 0;

