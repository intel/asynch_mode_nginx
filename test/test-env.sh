# Copyright (C) Intel, Inc

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
qat_poll_mode heuristic;}
}
"
export TEST_NGINX_GLOBALS_HTTPS="ssl_asynch on;"

export GZIP_DISABLE="
gzip off;
qatzip_sw only;
"

export GZIP_ENABLE="
gzip on;
qatzip_sw only;
"
export  GZIP_MIN_LENGTH_0="
gzip_min_length 0;
"

export  QATZIP_ENABLE="
qatzip_sw no;
qatzip_min_length 0;
"

export PROXY_ASYNCH_ENABLE="
proxy_ssl_asynch on;
"

export PROXY_ASYNCH_DISABLE="
proxy_ssl_asynch off;
"

export SSL_ASYNCH=" asynch"