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
    waitfor=2400
    TEST_NGINX_BINARY=$NGINX_INSTALL_DIR/sbin/nginx-for-test prove . >$SCRIPTPATH/nginx-test.log &
    commandpid=$!

    ( sleep $waitfor ; kill -9 $commandpid  > /dev/null 2>&1 ) &
    watchdog=$!
    sleeppid=$PPID

    wait $commandpid > /dev/null 2>&1
    kill $sleeppid > /dev/null 2>&1
    return 0
}

ModifiedToGzip()
{
    file_name=$1
    sed -i 's/#gzip on;*/gzip on;/' $file_name
    sed -i 's/#gzip_min_length 0;*/gzip_min_length 0;/' $file_name
    sed -i 's/ qatzip on;*/ #qatzip on;/' $file_name
    sed -i 's/ qatzip_min_length 0;*/ #qatzip_min_length 0;/' $file_name
}

ModifiedToQatzip()
{
    file_name=$1
    sed -i 's/ gzip on;*/ #gzip on;/' $file_name
    sed -i 's/ gzip_min_length 0;*/ #gzip_min_length 0;/' $file_name
    sed -i 's/#qatzip on;*/qatzip on;/' $file_name
    sed -i 's/#qatzip_min_length 0;*/qatzip_min_length 0;/' $file_name
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

#Configure according to the nginx test type
if [ "$#" == '1' ];then
  if [ $1 == 'qat' ];then
    cd $NGINX_SRC_DIR
    cp objs/ngx_ssl_engine_qat_module.so $NGINX_INSTALL_DIR/modules/ngx_ssl_engine_qat_module_for_test.so;
    cp objs/ngx_http_qatzip_filter_module.so $NGINX_INSTALL_DIR/modules/ngx_http_qatzip_filter_module_for_test.so
    cd $SCRIPTPATH;
    sed -i 's/use_engine dasync.*/use_engine qat;/' test-env.sh;
    sed -i 's/#default_algorithms ALL.*/default_algorithms ALL;/' test-env.sh;
    sed -i 's/#qat_engine.*/qat_engine {/' test-env.sh;
    sed -i 's/#qat_offload_mode async.*/qat_offload_mode async;/' test-env.sh;
    sed -i 's/#qat_notify_mode poll.*/qat_notify_mode poll;/' test-env.sh;
    sed -i 's/#qat_poll_mode heuristic;}.*/qat_poll_mode heuristic;}/' test-env.sh;
    sed -i 's/export TEST_DELAY_TIME.*/export TEST_DELAY_TIME="5"/' test-env.sh;
    sed -i 's/export TEST_LOAD_NGINX_MODULE.*/export TEST_LOAD_NGINX_MODULE="load_module $TEST_NGINX_MODULES\/ngx_ssl_engine_qat_module_for_test.so;"/' test-env.sh;
    sed -i 's/export TEST_LOAD_QATZIP_MODULE.*/export TEST_LOAD_QATZIP_MODULE="load_module $TEST_NGINX_MODULES\/ngx_http_qatzip_filter_module_for_test.so;"/' test-env.sh;
    ModifiedToQatzip nginx-tests/gzip.t
    ModifiedToQatzip nginx-tests/gzip_flush.t
    ModifiedToQatzip nginx-tests/h2.t
    ModifiedToQatzip nginx-tests/perl_gzip.t
    ModifiedToQatzip nginx-tests/proxy_cache.t
    ModifiedToQatzip nginx-tests/proxy_cache_vary.t
    ModifiedToQatzip nginx-tests/scgi_gzip.t
    ModifiedToQatzip nginx-tests/ssi_include_big.t
  elif [ $1 == 'dasync' ];then
    cd $SCRIPTPATH;
    sed -i 's/use_engine qat.*/use_engine dasync;/' test-env.sh;
    sed -i 's/.*default_algorithms ALL.*/#default_algorithms ALL;/' test-env.sh;
    sed -i 's/.*qat_engine.*/#qat_engine {/' test-env.sh;
    sed -i 's/.*qat_offload_mode async.*/#qat_offload_mode async;/' test-env.sh;
    sed -i 's/.*qat_notify_mode poll.*/#qat_notify_mode poll;/' test-env.sh;
    sed -i 's/.*qat_poll_mode heuristic;}.*/#qat_poll_mode heuristic;}/' test-env.sh;
    sed -i 's/export TEST_DELAY_TIME.*/export TEST_DELAY_TIME="0"/' test-env.sh;
    sed -i 's/export TEST_LOAD_NGINX_MODULE.*/export TEST_LOAD_NGINX_MODULE=""/' test-env.sh;
    sed -i 's/export TEST_LOAD_QATZIP_MODULE.*/export TEST_LOAD_QATZIP_MODULE=""/' test-env.sh;
    cp $OPENSSL_SRC_DIR/engines/dasync.so $OPENSSL_LIB/lib/engines-1.1
    ModifiedToGzip nginx-tests/gzip.t
    ModifiedToGzip nginx-tests/gzip_flush.t
    ModifiedToGzip nginx-tests/h2.t
    ModifiedToGzip nginx-tests/perl_gzip.t
    ModifiedToGzip nginx-tests/proxy_cache.t
    ModifiedToGzip nginx-tests/proxy_cache_vary.t
    ModifiedToGzip nginx-tests/scgi_gzip.t
    ModifiedToGzip nginx-tests/ssi_include_big.t
  else
    echo "The parameter qat or dasync needs to be passed in, read the README.md for more information."
    exit
  fi
else
    echo "The parameter qat or dasync needs to be passed in, read the README.md for more information."
    exit
fi
source ./test-env.sh
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

