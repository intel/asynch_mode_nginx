# Intel&reg; QuickAssist Technology (QAT) Async Mode Nginx
# Copyright (C) Intel, Inc
## Introduction
  Nginx-test is a tool for testing whether Nginx can run normally. The tool supports both qat and dasync modes.

## Run Nginx Basic Tests

**Set the following environmental variables:**

`NGINX_SRC_DIR` is the Nginx source code directory.<br/>
`NGINX_INSTALL_DIR` is the Nginx install directory.<br/>
`QATZIP_SRC_DIR` is the QATzip source code directory.<br/>
`OPENSSL_SRC_DIR` is the openssl source code directory.<br/>
`OPENSSL_LIB` is the openssl install directory.<br/>

**Execute the Nginx test:**

Choose one of the following commands to execute.
Passing the 'qat' parameter means testing with the QATZip (https://github.com/intel/QATZip) and QAT engine (https://github.com/intel/QAT_Engine) loaded.
Passing the 'dasync' parameter means testing with the OpenSSL built-in dasync engine loaded and no QATZip aceleration employed.

```bash
    ./nginx-test.sh qat
```

```bash
    ./nginx-test.sh dasync
```

**View execution log**
If the result contains ‘skipped’, it means that the conditions required for the test are missing.
If the result in the log is PASS, it means that all the scripts that satisfy the test are successfully tested,
otherwise the test fails.

```bash
    vim nginx-test.log
```

## nginx_qat_module
Any 3rd party OpenSSL engine modules can be integrated into this framework. By default, a
reference module `dasync_module` is provided in `src/engine/modules`
and a QAT module `nginx_qat_module` is provided in `modules/nginx_qat_modules`.

If the passed-in argument for the test is qat, the qat module 'ngx_ssl_engine_qat_module_for_test.so' needs to be loaded,
and the 'qat_engine' block needs to be configured.
If the passed-in argument for the test is dasync, just configure 'use_engine' directive as dasync.
The variable parameters TEST_NGINX_SSL_ENGINE_MODULE and TEST_LOAD_MODULE in 'test-env.sh'
change the configuration based on the parameters passed in.

Passing the 'qat' parameter:
```bash
   load_module modules/ngx_ssl_engine_qat_module.so;
   ...
   ssl_engine {
       use_engine qatengine;
       default_algorithms ALL;
           qat_engine {
               qat_offload_mode async;
               qat_notify_mode poll;
               qat_poll_mode heuristic;
           }
   }
```

Passing the 'dasync' parameter:
```bash
   ssl_engine {
        use_engine qat;
        ...
    }
```
For more details directives of `nginx_qat_module`, please refer to
`modules/nginx_qat_modules/README`.

## nginx_qatzip_module
This module is developed for accelerating GZIP compression with QATzip in Nginx
dynamic module framework.

For more details directives of `nginx_qatzip_module`, please refer to
`modules/nginx_qatzip_module/README`.

Using QATZip for compression:
```bash
   load_module modules/ngx_http_qatzip_filter_module.so;
   ...
   qatzip_sw no;
```