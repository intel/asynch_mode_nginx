# Intel&reg; QuickAssist Technology (QAT) Async Mode Nginx

## Table of Contents

- [Introduction](#introduction)
- [Licensing](#licensing)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Additional Information](#additional-information)
- [Limitations](#limitations)
- [Installation Instructions](#installation-instructions)
    - [Install Async Mode Nginx](#install-async-mode-nginx)
    - [Build OpenSSL\* and QAT engine](#build-openssl-and-qat-engine)
    - [Build QATzip](#build-qatzip)
    - [Run Nginx official test](#run-nginx-official-test)
- [SSL Engine Framework for Configuration](#ssl-engine-framework-for-configuration)
- [Support for Nginx Side Polling](#support-for-nginx-side-polling)
    - [External Polling Mode](#external-polling-mode)
    - [Heuristic Polling Mode](#heuristic-polling-mode)
- [QATzip Module Configuration](#qatzip-module-configuration)
- [QAT Sample Configuration](#qat-sample-configuration)
- [Known Issues](#known-issues)
- [Intended Audience](#intended-audience)
- [Legal](#legal)

## Introduction

Nginx\* [engine x] is an HTTP and reverse proxy server, a mail proxy server,
and a generic TCP/UDP proxy server, originally written by Igor Sysoev.
This project provides an extended Nginx working with asynchronous mode OpenSSL\*.
With Intel&reg; QuickAssist Technology (QAT) acceleration, the asynchronous mode Nginx
can provide significant performance improvement.

## Licensing

The Licensing of the files within this project is:

Intel&reg; Quickassist Technology (QAT) Async Mode Nginx - BSD License. Please
see the `LICENSE` file contained in the top level folder. Further details can
be found in the file headers of the relevant files.

## Features

* Asynchronous Mode in SSL/TLS processing (including http/stream/mail/proxy module)
* SSL Engine Framework for engine configuration
* Support for external polling mode and heursitic polling mode
* Release hardware resource during worker is shutting down (For more details
  information, please read modules/nginx_qat_module/README)
* Support OpenSSL Cipher PIPELINE feature
* Support QATzip module to accelerate GZIP compression with Intel&reg; Quickassist Technology
* Support software fallback for asymmetric cryptography algorithms.
* Support [QAT Engine multibuffer feature][10]
[10]:https://github.com/intel/QAT_Engine#intel-qat-openssl-engine-multibuffer-support

## Hardware Requirements

Async Mode Nginx supports Crypto and Compression offload to the following acceleration devices:

* [Intel&reg; C62X Series Chipset][1]
* [Intel&reg; Communications Chipset 8925 to 8955 Series][2]
* [Intel&reg; Communications Chipset 8960 to 8970 Series][9]

[1]:https://www.intel.com/content/www/us/en/design/products-and-solutions/processors-and-chipsets/purley/intel-xeon-scalable-processors.html
[2]:https://www.intel.com/content/www/us/en/ethernet-products/gigabit-server-adapters/quickassist-adapter-8950-brief.html
[9]:https://www.intel.com/content/www/us/en/ethernet-products/gigabit-server-adapters/quickassist-adapter-8960-8970-brief.html

## Software Requirements

This release was validated on the following:

* Async Mode Nginx has been tested with the latest Intel&reg; QuickAssist Acceleration Driver.
Please download the QAT driver from the link https://01.org/intel-quickassist-technology
* OpenSSL-1.1.1g
* QAT engine v0.5.44
* QATzip v1.0.1

## Additional Information

* Async Mode Nginx is developed based on Nginx-1.16.1.

* Generate Async Mode Nginx patch against official Nginx-1.16.1.

```bash
  git clone https://github.com/intel/asynch_mode_nginx.git
  wget http://nginx.org/download/nginx-1.16.1.tar.gz
  tar -xvzf ./nginx-1.16.1.tar.gz
  diff -Naru -x .git nginx-1.16.1 asynch_mode_nginx > async_mode_nginx_1.16.1.patch
```

* Apply Async Mode Nginx patch to official Nginx-1.16.1.

```bash
  wget http://nginx.org/download/nginx-1.16.1.tar.gz
  tar -xvzf ./nginx-1.16.1.tar.gz
  patch -p0 < async_mode_nginx_1.16.1.patch
```

* Async Mode Nginx SSL engine framework provides new directives:

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
```

```bash
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

* Async Mode Nginx provide new option `asynch` for `listen` directive.

**Example**

file: conf/nginx.conf

```bash
    http {
        server{ listen 443 asynch; }
    }
```

* Support OpenSSL Cipher PIPELINE feature (Deitals information about the pipeline
  settings, please refer to [OpenSSL Docs][6])

**Directives**
```bash
Syntax:     ssl_max_pipelines size;
Default:    ssl_max_pipelines 0;
Context:    server

Set MAX number of pipelines
```

**Directives**
```bash
Syntax:     ssl_split_send_fragment size;
Default:    ssl_split_send_fragment 0;
Context:    server

Set split size of sending fragment
```

**Directives**
```bash
Syntax:     ssl_max_send_fragment size;
Default:    ssl_max_send_fragment 0;
Context:    server

Set max number of sending fragment
```

* [White Paper: Intel&reg; Quickassist Technology and OpenSSL-1.1.0:Performance][3]

[3]: https://01.org/sites/default/files/downloads/intelr-quickassist-technology/intelquickassisttechnologyopensslperformance.pdf
[6]: https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_split_send_fragment.html

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

* When configure "worker_process auto", async Nginx will need instance number equal or larger than
  2 times of CPU core number. Otherwise, async-Nignx might show various issue caused by leak of
  instance.

* Nginx supports QAT engine and QATzip module. By default, they use User Space
  DMA-able Memory (USDM) Component. The USDM component is located within the
  Upstream Intel&reg; QAT Driver source code in the following subdirectory:
  `quickassist/utilities/libusdm_drv`. We should have this component enabled
  and assignd enough memory before using nginx_qat_module or
  nginx_qatzip_module, for example:

    ```bash
        echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
        insmod ./usdm_drv.ko max_huge_pages=2048 max_huge_pages_per_process=32
    ```

## Installation Instructions

### Install Async Mode Nginx

**Set the following environmental variables:**

`NGINX_INSTALL_DIR` is the directory where Nginx will be installed to

`OPENSSL_LIB` is the directory where the OpenSSL has been installed to

`QZ_ROOT` is the directory where the QATzip has been compiled to

**Configure nginx for compilation:**

```bash
    ./configure \
        --prefix=$NGINX_INSTALL_DIR \
        --with-http_ssl_module \
        --add-dynamic-module=modules/nginx_qatzip_module \
        --add-dynamic-module=modules/nginx_qat_module/ \
        --with-cc-opt="-DNGX_SECURE_MEM -I$OPENSSL_LIB/include -I$QZ_ROOT/include -Wno-error=deprecated-declarations" \
        --with-ld-opt="-Wl,-rpath=$OPENSSL_LIB/lib -L$OPENSSL_LIB/lib -L$QZ_ROOT/src -lqatzip -lz"
```

**Compile and Install:**

```bash
    make
    make install
```

** Nginx supports setting worker to non-root user, for example:**

    Add user qat in group qat, for example run below command in your terminal:
    ```bash
        groupadd qat
        useradd -g qat qat
    ```

    In nginx.conf, you can set worker as qat, qat is the user you added before:
    ```bash
        user qat qat;
    ```

    Then we need to give non-root worker enough permission to enable qat, you need to run folow
    connamds in your terminal:
    ```bash
        chgrp qat /dev/qat_*
        chmod 660 /dev/qat_*
        chgrp qat /dev/usdm_drv
        chmod 660 /dev/usdm_drv
        chgrp qat /dev/uio*
        chmod 660 /dev/uio*
        chgrp qat /dev/hugepages
        chmod 770 /dev/hugepages
        chgrp qat /usr/local/lib/libqat_s.so
        chgrp qat /usr/local/lib/libusdm_drv_s.so
    ```

### Build OpenSSL\* and QAT engine

These instructions can be found on [QAT engine][4]

[4]: https://github.com/intel/QAT_Engine#installation-instructions

### Build QATzip

These instructions can be found on [QATzip][5]

[5]: https://github.com/intel/QATzip#installation-instructions

### Run Nginx official test

These instructions can be found on [Official test][8]

[8]: https://github.com/intel/asynch_mode_nginx/blob/master/test/README.md

## SSL Engine Framework for Configuration

As QAT engine is implemented as a standard OpenSSL\* engine, its behavior can be
controlled from the OpenSSL\* configuration file (`openssl.conf`), which can be
found on [QAT engine][7].

[7]: https://github.com/intel/QAT_Engine#using-the-openssl-configuration-file-to-loadinitialize-engines

**Note**:
From v0.3.2 and later, this kind of configuration in `openssl.conf` will not be
effective for Nginx. Please use the following method to configure Nginx SSL
engine, such as Intel&reg; QAT.

An SSL Engine Framework is introduced to provide a more powerful and flexible
mechanism to configure Nginx SSL engine directly in the Nginx configuration file
(`nginx.conf`).

### ssl_engine configuration
A new configuration block is introduced as `ssl_engine` which provides two
general directives:

```bash
Syntax:     use_engine [engine name];
Default:    N/A
Context:    ssl_engine
Description:
            Specify the engine name
```

```bash
Syntax:     default_algorithms [ALL/RSA/EC/...];
Default:    ALL
Context:    ssl_engine
Description:
            Specify the algorithms need to be offloaded to the engine
            More details information please refer to OpenSSL engine
```

A following configuration sub-block can be used to set engine specific
configuration. The name of the sub-block should have a prefix using the
engine name specified in `use_engine`, such as `[engine_name]_engine`.

### nginx_qat_module
Any 3rd party modules can be integrated into this framework. By default, a
reference module `dasync_module` is provided in `src/engine/modules`
and a QAT module `nginx_qat_module` is provided in `modules/nginx_qat_modules`.

An example configuration in the `nginx.conf`:
```bash
    load_module modules/ngx_ssl_engine_qat_module.so;
    ...
    ssl_engine {
        use_engine qat;
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

## Support for Nginx Side Polling
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
        use_engine qat;
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
        use_engine qat;
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

## QATzip Module Configuration

This module is developed to accelerate GZIP compression with QAT accelerators
through QATzip stream API released in v0.2.6.
Software fallback feature of QATzip is released in v1.0.0.

**Note:**

* This mode is only available when using QATzip v1.0.0 or later.
* This mode relys on gzip module for SW fallback feature.
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

## QAT Sample Configuration

file: `conf/nginx.QAT-sample.conf`

This is a sample configure file shows how to configure QAT in nginx.conf. This file includes:

* Enable SSL QAT engine in heretic mode.
* Support HTTPS async mode.
* Enable QATzip support.
* Select TLS-1.2 as the default ssl_protocols.

**Note:**

* The QAT configuration needs crypto and compression instance for the user space application.

## Known Issues
**'Orphan ring' errors in `dmesg` output when Nginx exit**<br/>
   Working with current QAT driver (version 4.6.0 in 01.org), Nginx workers exit
   with 'Orphan ring' errors. This issue has been fixed in future QAT driver release

**Cache manager/loader process will allocate QAT instance via QAT engine**<br/>
   According to current QAT engine design, child process forked by master
   process will initialize QAT engine automatically in QAT engine `atfork`
   hook function. If cache manager/loader processes are employed, QAT instances
   will be allocated in the same way as worker process. Cache manager/loader
   processes do not perform modules' `exit process` method in Nginx native design
   which will introduce "Orphan ring" error message in `dmesg` output.

**QATzip module does not support dictionary compression**<br/>
   QATzip module supports GZIP compression acceleration now, does not support
   user define dictionary compression yet.

**Segment fault happens while sending HUP signal when QAT instances not enough**<br/>
   If the available qat instance number is less than 2x Nginx worker process number, segment fault
   happens while sending HUP signal to Async Mode Nginx. Using `qat_sw_fallback on;` in qat_engine
   directive as a workaround for this issue. And it needs special attention if the QAT instances
   are enough when setting `worker_processes auto;`.

**Insufficient HW resources would cause segment fault while sending HUP signal**<br/>
   Before running nginx, please make sure you have arranged enough HW resources for nginx, including
   memory and hard disk space. Disk space exhausted or out of memory would cause core dump when
   nginx receives HUP signal during handshake phase.

**TLS1.3 Early data function may failed when enable HKDF offload in QAT Engine**<br/>
   When enable HKDF offload in QAT Engine, and enable early data function with TLS1.3 protocol in
   Nginx configuration, early data operation in session reuse case may failed.

**Core-dump happened when reload nginx worker with ssl_engine removed from nginx.conf**<br/>
   Start Async Mode Nginx with ssl_engine directive in nginx.conf, then remove the ssl_engine
   directive and reload Async Mode Nginx with command `nginx -s reload`, will cause coredump.
   Need to avoid this kind of operation currently.

## Intended Audience

The target audience may be software developers, test and validation engineers,
system integrators, end users and consumers for Async Mode Nginx integrated
Intel&reg; Quick Assist Technology.

## Legal

Intel&reg; disclaims all express and implied warranties, including without
limitation, the implied warranties of merchantability, fitness for a
particular purpose, and non-infringement, as well as any warranty arising
from course of performance, course of dealing, or usage in trade.

This document contains information on products, services and/or processes in
development.  All information provided here is subject to change without
notice. Contact your Intel&reg; representative to obtain the latest forecast
, schedule, specifications and roadmaps.

The products and services described may contain defects or errors known as
errata which may cause deviations from published specifications. Current
characterized errata are available on request.

Copies of documents which have an order number and are referenced in this
document may be obtained by calling 1-800-548-4725 or by visiting
www.intel.com/design/literature.htm.

Intel, the Intel logo are trademarks of Intel Corporation in the U.S.
and/or other countries.

\*Other names and brands may be claimed as the property of others

