# Asynch Mode for NGINX\*

Nginx ("engine x") is an HTTP web server, reverse proxy, content cache,
load balancer, TCP/UDP proxy server, and mail proxy server.
Originally written by Igor Sysoev and distributed under the 2-clause BSD License.
This project provides async capabilities to Nginx using OpenSSL\* ASYNC infrastructure.
The Async mode Nginx\* with Intel&reg; QuickAssist Technology (QAT) Acceleration can
provide significant performance improvements.

## Table of Contents

- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Installation Instructions](#installation-instructions)
- [Testing](#testing)
    - [Official Unit tests](#official-unit-tests)
    - [Performance Testing](#performance-testing)
- [QAT Configuration](#qat-configuration)
    - [SSL Engine (QAT Engine) Configuration](#ssl-engine-qat-engine-configuration)
    - [Nginx Side Polling](#nginx-side-polling)
    - [QATzip Module Configuration](#qatzip-module-configuration)
- [Additional Information](#additional-information)
- [Limitations](#limitations)
- [Known Issues](#known-issues)
- [Licensing](#licensing)
- [Legal](#legal)

## Features

* Asynchronous Mode in SSL/TLS processing (including http/stream/mail/proxy module)
* Support QAT_HW and QAT_SW PKE and symmetric Crypto acceleration
* Support QAT_HW GZIP compression acceleration
* SSL Engine Framework for engine configuration
* Support for external polling mode and heuristic polling mode
* Release hardware resource during worker is shutting down (For more details
  information, please read modules/nginx_qat_module/README)
* Support OpenSSL Cipher PIPELINE Capability.
* Support software fallback for asymmetric cryptography algorithms(Linux & FreeBSD) and symmetric
  algorithms (FreeBSD only)

## Hardware Requirements

Asynch Mode for NGINX\* supports QAT_HW Crypto and Compression acceleration
on the platforms with the following QAT devices

* [Intel® QuickAssist 4xxx Series][1]
* [Intel® QuickAssist Adapter 8970][2]
* [Intel® QuickAssist Adapter 8960][3]
* [Intel® QuickAssist Adapter 8950][4]
* [Intel® Atom&trade; Processor C3000][5]

QAT_SW acceleration is supported in the platforms starting with [3rd Generation Intel&reg; Xeon&reg; Scalable Processors family][6] and later.

[1]:https://www.intel.com/content/www/us/en/products/details/processors/xeon/scalable.html
[2]:https://www.intel.com/content/www/us/en/products/sku/125200/intel-quickassist-adapter-8970/downloads.html
[3]:https://www.intel.com/content/www/us/en/products/sku/125199/intel-quickassist-adapter-8960/downloads.html
[4]:https://www.intel.com/content/www/us/en/products/sku/80371/intel-communications-chipset-8950/specifications.html
[5]:https://www.intel.com/content/www/us/en/design/products-and-solutions/processors-and-chipsets/denverton/ns/atom-processor-c3000-series.html
[6]:https://www.intel.com/content/www/us/en/products/docs/processors/xeon/3rd-gen-xeon-scalable-processors-brief.html

## Software Requirements

This release was validated on the following and its dependent libraries
mentioned QAT Engine and QATZip software Requirements section.

* [OpenSSL-3.0.14](https://github.com/openssl/openssl)
* [QAT engine v1.6.2](https://github.com/intel/QAT_Engine)
* [QATzip v1.2.0](https://github.com/intel/QATzip)

## Installation Instructions

Pre-requisties: Install OpenSSL and QAT Engine(for Crypto using QAT_HW & QAT_SW) and QATZip(for Compression)
using the [QAT engine](https://github.com/intel/QAT_Engine#installation-instructions)
and [QATzip](https://github.com/intel/QATzip#installation-instructions) installation instructions. 

**Set the following environmental variables:**

`NGINX_INSTALL_DIR` is the directory where Nginx will be installed to

`OPENSSL_LIB` is the directory where the OpenSSL has been installed to

`QZ_ROOT` is the directory where the QATzip has been compiled to

**Configure and build nginx with QAT Engine and QATZip**

```bash
./configure \
  --prefix=$NGINX_INSTALL_DIR \
  --with-http_ssl_module \
  --add-dynamic-module=modules/nginx_qatzip_module \
  --add-dynamic-module=modules/nginx_qat_module/ \
  --with-cc-opt="-DNGX_SECURE_MEM -I$OPENSSL_LIB/include -I$ICP_ROOT/quickassist/include -I$ICP_ROOT/quickassist/include/dc -I$QZ_ROOT/include -Wno-error=deprecated-declarations" \
  --with-ld-opt="-Wl,-rpath=$OPENSSL_LIB/lib64 -L$OPENSSL_LIB/lib64 -L$QZ_ROOT/src -lqatzip -lz"
  make
  make install
```

* Refer QAT Settings [here](https://intel.github.io/quickassist/GSG/2.X/installation.html#running-applications-as-non-root-user) for running Nginx
under non-root user.

Also there is dockerfile available for Async mode nginx with QATlib which can be built into docker images. Please refer [here](dockerfiles/README.md) for more details.

## Testing

### Official Unit tests

These instructions can be found on [Official test](https://github.com/intel/asynch_mode_nginx/blob/master/test/README.md)

### Performance Testing

Performance Testing with Async mode Nginx along with best known build and
runtime configuration along with scripts available [here](https://intel.github.io/quickassist/qatlib/asynch_nginx.html)

## QAT Configuration

Refer Sample Configuration file `conf/nginx.QAT-sample.conf` with QAT configured which includes

* SSL QAT engine in heuristic mode.
* HTTPS in asynchronous mode.
* QATZip Module for compression acceleration.
* TLS1.3 Support as default.

* The QAT Hardware driver configuration needs crypto and compression instance configured

### SSL Engine (QAT Engine) configuration
An SSL Engine Framework is introduced to provide a more powerful and flexible
mechanism to configure Nginx SSL engine directly in the Nginx configuration file
(`nginx.conf`).

Loading Engine via OpenSSL.cnf is not viable in Nginx. Please use the SSL Engine
Framework which provides a more powerful and flexible mechanism to configure
Nginx SSL engine such as Intel&reg; QAT Engine directly in the Nginx
configuration file (`nginx.conf`).

A new configuration block is introduced as `ssl_engine` which provides two
general directives:

Sets the engine module and engine id for OpenSSL async engine. For example:
```bash
Syntax:     use_engine [engine_module_name] [engine_id];
Default:    N/A
Context:    ssl_engine
Description:
            Specify the engine module name against engine id
```

where engine_module_name is optional and should be same as engine id if it is not used.

```bash
Syntax:     default_algorithms [ALL/RSA/EC/...];
Default:    ALL
Context:    ssl_engine
Description:
            Specify the algorithms need to be offloaded to the engine
            More details information please refer to OpenSSL engine
```
If Handshake only acceleration is preferred then default_algorithms should `RSA EC DH DSA PKEY`
and if symmetric only is preferred then use `CIPHERS`. If all algorithms are preferred then `ALL`
must be specified.

A following configuration sub-block can be used to set engine specific
configuration. The name of the sub-block should have a prefix using the
engine name specified in `use_engine`, such as `[engine_name]_engine`.

Any 3rd party modules can be integrated into this framework. By default, a
reference module `dasync_module` is provided in `src/engine/modules`
and a QAT module `nginx_qat_module` is provided in `modules/nginx_qat_modules`.

An example configuration in the `nginx.conf`:
```bash
    load_module modules/ngx_ssl_engine_qat_module.so;
    ...
    ssl_engine {
        use_engine qatengine;
        default_algorithms RSA,EC,DH,PKEY_CRYPTO;
        qat_engine {
            qat_sw_fallback on;
            qat_offload_mode async;
            qat_notify_mode poll;
            qat_poll_mode heuristic;
            qat_shutting_down_release on;
        }
    }
```
For more details directives of `nginx_qat_module`, please refer to
`modules/nginx_qat_modules/README`.

## Nginx Side Polling
The qat_module provides two kinds of Nginx side polling for QAT engine,

* external polling mode
* heuristic polling mode

Please refer to the README file in the `modules/nginx_qat_modules` directory
to install this dynamic module.

**Note**:
External polling and heuristic polling are unavailable in SSL proxy and stream
module up to current release.

### External Polling Mode

A timer-based polling is employed in each Nginx worker process to collect
accelerator's response.

**Directives in the qat_module**
```bash
Syntax:     qat_external_poll_interval time;
Default:    1
Dependency: Valid if (qat_poll_mode=external)
Context:    qat_engine
Description:
            External polling time interval (ms)
            Valid value: 1 ~ 1000
```
**Example**
file: conf/nginx.conf

```bash
    load_module modules/ngx_ssl_engine_qat_module.so;
    ...
    ssl_engine {
        use_engine qatengine;
        default_algorithms ALL;
        qat_engine {
            qat_offload_mode async;
            qat_notify_mode poll;

            qat_poll_mode external;
            qat_external_poll_interval 1;
        }
    }
```


### Heuristic Polling Mode

This mode can be regarded as an improvement of the timer-based external polling.
It is the recommended polling mode to communicate with QAT accelerators because
of its performance advantages. With the knowledge of the offload traffic, it
controls the response reaping rate to match the request submission rate so as to
maximize system efficiency with moderate latency, and adapt well to diverse
types of network traffics.

**Note:**

* This mode is only available when using QAT engine v0.5.35 or later.
* External polling timer is enabled by default when heuristic polling mode is enabled.

In the heuristic polling mode, a polling operation is only triggered at a proper moment:

* Number of in-flight offload requests reaches a pre-defined threshold (in consideration of efficiency)
* All the active SSL connections have submitted their cryptographic requests and been waiting for
the corresponding responses (in consideration of timeliness).

**Directives in the qat_module**
```bash
Syntax:     qat_heuristic_poll_asym_threshold num;
Default:    48
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_engine
Description:
            Threshold of the number of in-flight requests to trigger a polling
            operation when there are in-flight asymmetric crypto requests
            Valid value: 1 ~ 512


Syntax:     qat_heuristic_poll_sym_threshold num;
Default:    24
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_engine
Description:
            Threshold of the number of in-flight requests to trigger a polling
            operation when there is no in-flight asymmetric crypto request
            Valid value: 1 ~ 512
```
**Example**
file: `conf/nginx.conf`

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
            qat_heuristic_poll_asym_threshold 48;
            qat_heuristic_poll_sym_threshold 24;
        }
    }
```

### QATzip Module Configuration

This module is developed to accelerate GZIP compression with QAT accelerators
through QATzip stream API released in v0.2.6.

**Note:**

* This mode is only available when using QATzip v1.0.0 or later.
* This mode relies on gzip module for SW fallback feature.
* The qatzip_sw is set to failover by default, do not load QATzip module if you
do not want to enable qatzip. Or else it would be enabled and set to failover.

**Directives in the qatzip_module**
```bash
    Syntax:     qatzip_sw only/failover/no;
    Default:    qatzip_sw failover;
    Context:    http, server, location, if in location
    Description:
                only: qatzip is disable, using gzip;
                failover: qatzip is enable, qatzip sfotware fallback feature enable.
                no: qatzip is enable, qatzip sfotware fallback feature disable.

    Syntax:     qatzip_chunk_size size;
    Default:    qatzip_chunk_size 64k;
    Context:    http, server, location
    Description:
                Sets the chunk buffer size in which data will be compressed into
                one deflate block. By default, the buffer size is equal to 64K.

    Syntax:     qatzip_stream_size size;
    Default:    qatzip_stream_size 256k;
    Context:    http, server, location
    Description:
                Sets the size of stream buffers in which data will be compressed into
                multiple deflate blocks and only the last block has FINAL bit being set.
                By default, the stream buffer size is 256K.
```

**Example**
file: `conf/nginx.conf`

```bash
    load_module modules/ngx_http_qatzip_filter_module.so;
    ...

    gzip_http_version   1.0;
    gzip_proxied any;
    qatzip_sw failover;
    qatzip_min_length 128;
    qatzip_comp_level 1;
    qatzip_buffers 16 8k;
    qatzip_types text/css text/javascript text/xml text/plain text/x-component application/javascript application/json application/xml application/rss+xml font/truetype font/opentype application/vnd.ms-fontobject image/svg+xml application/octet-stream image/jpeg;
    qatzip_chunk_size   64k;
    qatzip_stream_size  256k;
    qatzip_sw_threshold 256;
```
For more details directives of `nginx_qatzip_module`, please refer to
`modules/nginx_qatzip_modules/README`.

## Additional Information

### Generating Async mode Patch
* Asynch Mode for NGINX\* is developed based on Nginx-1.26.2. Refer steps below
to generate patch to apply on top of official Nginx-1.26.2.

#### Generating the patch with package from Nginx download page
```bash
  git clone https://github.com/intel/asynch_mode_nginx.git asynch_mode_nginx
  wget http://nginx.org/download/nginx-1.26.2.tar.gz
  tar -xvzf nginx-1.26.2.tar.gz
  diff -Naru -x .git nginx-1.26.2 asynch_mode_nginx > async_mode_nginx_1.26.2.patch
  # Apply Patch
  cd nginx-1.26.2
  patch -p1 < ../async_mode_nginx_1.26.2.patch
```

#### Generating the patch with package from Github Mirror page

```bash
  git clone https://github.com/intel/asynch_mode_nginx.git
  wget https://github.com/nginx/nginx/archive/release-1.26.2.tar.gz
  tar -xvzf release-1.26.2.tar.gz
  diff -Naru -x .git -x .hgtags nginx-release-1.26.2 asynch_mode_nginx > async_mode_nginx_1.26.2.patch
  # Apply Patch
  patch -p0 < async_mode_nginx_1.26.2.patch
```

### New Async Directives

**Directives**
```bash
Syntax:     ssl_asynch on | off;
Default:    ssl_asynch off;
Context:    stream, mail, http, server

    Enables SSL/TLS asynchronous mode
```
**Example**

file: conf/nginx.conf

```bash
    http {
        ssl_asynch  on;
        server {...}
    }

    stream {
        ssl_asynch  on;
        server {...}
    }
```

**Directives**
```bash
Syntax:     proxy_ssl_asynch on | off;
Default:    proxy_ssl_asynch off;
Context:    stream, http, server

Enables the SSL/TLS protocol asynchronous mode for connections to a proxied server.
```

**Example**

file: conf/nginx.conf

```bash
    http {
        server {
            location /ssl {
                proxy_pass https://127.0.0.1:8082/outer;
                proxy_ssl_asynch on;
            }
        }
    }
```

**Directives**
```bash
Syntax:     grpc_ssl_asynch on | off;
Default:    grpc_ssl_asynch off;
Context:    http, server

Enables the SSL/TLS protocol asynchronous mode for connections to a grpc server.
```

**Example**

file: conf/nginx.conf

```bash
    http {
        server {
            location /grpcs {
                grpc_pass https://127.0.0.1:8082/outer;
                grpc_ssl_asynch on;
            }
        }
    }
```

* OpenSSL Cipher PIPELINE Capability. Refer [OpenSSL Docs](https://docs.openssl.org/1.1.1/man3/SSL_CTX_set_split_send_fragment)
for detailed information.

**Directives**
```bash
Syntax:     ssl_max_pipelines size;
Default:    ssl_max_pipelines 0;
Context:    server

Set MAX number of pipelines

Syntax:     ssl_split_send_fragment size;
Default:    ssl_split_send_fragment 0;
Context:    server

Set split size of sending fragment

Syntax:     ssl_max_send_fragment size;
Default:    ssl_max_send_fragment 0;
Context:    server

Set max number of sending fragment
```

## Limitations

* Nginx supports `reload` operation, when QAT hardware is involved for crypto
  offload, user should ensure that there are enough number of QAT instances.
  For example, the available qat instance number should be 2x equal or more than
  Nginx worker process number.

  For example, in Nginx configuration file (`nginx.conf`) worker process number
  is configured as

    ```bash
       worker_processes 16;
    ```

    Then the instance configuration in QAT driver configuration file should be

    ```bash
        [SHIM]
        NumberCyInstances = 1
        NumberDcInstances = 0
        NumProcesses = 32
        LimitDevAccess = 0
    ```

* When configure "worker_process auto", Asynch Mode for NGINX\* will need instance number equal or larger than
  2 times of CPU core number. Otherwise, Asynch Mode for NGINX\* might show various issue caused by leak of
  instance.

* Nginx supports QAT engine and QATzip module. By default, they use User Space
  DMA-able Memory (USDM) Component. The USDM component is located within the
  Upstream Intel&reg; QAT Driver source code in the following subdirectory:
  `quickassist/utilities/libusdm_drv`. We should have this component enabled
  and assigned enough memory before using nginx_qat_module or
  nginx_qatzip_module, for example:

    ```bash
        echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
        insmod ./usdm_drv.ko max_huge_pages=2048 max_huge_pages_per_process=32
    ```

* AES-CBC-HMAC-SHA algorithm won't be offloaded to QAT_HW if Encrypt then MAC(ETM) mode
  is used for SSL connection(by default). Refer to [QAT_Engine Limitations](https://github.com/intel/QAT_Engine/blob/master/docs/limitations.md#limitations)
  for more details.

* QATzip module supports GZIP compression acceleration now, does not support
  user define dictionary compression yet.

* The HTTP2 protocol does not support asynchronous functionality.

* QUIC is not supported for Async nginx with QAT.

## Known Issues
**'Orphan ring' errors in `dmesg` output when Nginx exit**<br/>
   Working with current QAT driver, Nginx workers exit with 'Orphan ring'
   errors. This issue has been fixed in future QAT driver release.

**Cache manager/loader process will allocate QAT instance via QAT engine**<br/>
   According to current QAT engine design, child process forked by master
   process will initialize QAT engine automatically in QAT engine `atfork`
   hook function. If cache manager/loader processes are employed, QAT instances
   will be allocated in the same way as worker process. Cache manager/loader
   processes do not perform modules' `exit process` method in Nginx native design
   which will introduce "Orphan ring" error message in `dmesg` output.

**Segment fault happens while sending HUP signal when QAT instances not enough**<br/>
   If the available qat instance number is less than 2x Nginx worker process number, segment fault
   happens while sending HUP signal to Asynch Mode for NGINX\*. Using `qat_sw_fallback on;` in qat_engine
   directive as a workaround for this issue. And it needs special attention if the QAT instances
   are enough when setting `worker_processes auto;`.

**Insufficient HW resources would cause segment fault while sending HUP signal**<br/>
   Before running nginx, please make sure you have arranged enough HW resources for nginx, including
   memory and hard disk space. Disk space exhausted or out of memory would cause core dump when
   nginx receives HUP signal during handshake phase.

**Performance drop under OpenSSL 3.0**<br/>
   * Both ECDH and PRF cause performance drop under OpenSSL 3.0.
   * Due to changes in the OpenSSL 3.0 framework, TLS handshake performance is significantly degraded. 
     Refer [#21833](https://github.com/openssl/openssl/issues/21833) and associated issues for more details.

**The 0-RTT (early data) issue**<br/>
  The 0-RTT (early data) feature does not support async mode in current asynch_mode_nginx,
  so it's not recommended to use async offload to QAT hardware during early data process.

**CHACHA-POLY and AES-GCM throughput performance issue**<br/>
  With the bottleneck of memory allocation, the throughput of CHACHA-Poly and
  AES-GCM can not reach the peak value when running with 4 or more QAT devices.

## Licensing

Released under BSD License. Please see the `LICENSE` file contained in the top
level folder. Further details can be found in the file headers of the relevant files.

## Legal

Intel, Intel Atom, and Xeon are trademarks of Intel Corporation in the U.S. and/or other countries.

*Other names and brands may be claimed as the property of others.

Copyright © 2014-2024, Intel Corporation. All rights reserved.
