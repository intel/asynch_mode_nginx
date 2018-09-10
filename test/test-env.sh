# (C) Intel, Inc.

export TEST_NGINX_MODULES=$NGINX_INSTALL_DIR/modules
export TEST_NGINX_SSL_ENGINE_MODULE="ssl_engine {
use_engine qat;
default_algorithms ALL;
qat_engine {
qat_offload_mode async;
qat_notify_mode poll;
qat_poll_mode heuristic;}
}"
export TEST_NGINX_SSL_ASYNCH="ssl_asynch on;"
export TEST_DELAY_TIME="5"
export TEST_LOAD_MODULE="load_module $TEST_NGINX_MODULES/ngx_ssl_engine_qat_module_for_test.so;"
