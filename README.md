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
- [SSL Engine Framework for Configuration](#ssl-engine-framework-for-configuration)
- [Support for Nginx level Polling](#support-for-nginx-side-polling)
    - [External Polling Mode](#external-polling-mode)
    - [Heuristic Polling Mode](#heuristic-polling-mode)
- [Known Issues](#known-issues)
- [Intended Audience](#intended-audience)
- [Legal](#legal)

## Introduction

Nginx\* [engine x] is an HTTP and reverse proxy server, a mail proxy server,
and a generic TCP/UDP proxy server, originally written by Igor Sysoev.
This project provides an extended Nginx working with asynchronous mode OpenSSL\*.
With Intel&reg; QuickAssist Technology(QAT) acceleration, the asynchronous mode Nginx
can provide significant performance improvement.

## Licensing

The Licensing of the files within this project is:

Intel&reg; Quickassist Technology (QAT) Async Mode Nginx - BSD License. Please
see the `LICENSE` file contained in the top level folder. Further details can
be found in the file headers of the relevant files.

## Features

* Asynchronous Mode in SSL/TLS processing
* SSL Engine Framework for engine configuraion
* Support for external polling mode and heursitic polling mode
* Release hardware resource during worker is shutting down (For more details
  information, please read modules/nginx_qat_module/README)

## Hardware Requirements

Async Mode Nginx supports Crypto offload to the following acceleration devices:

* [Intel&reg; C62X Series Chipset][1]
* [Intel&reg; Communications Chipset 8925 to 8955 Series][2]

[1]:https://www.intel.com/content/www/us/en/design/products-and-solutions/processors-and-chipsets/purley/intel-xeon-scalable-processors.html
[2]:https://www.intel.com/content/www/us/en/ethernet-products/gigabit-server-adapters/quickassist-adapter-8950-brief.html

## Software Requirements

This release was validated on the following:

* OpenSSL-1.1.0h
* QAT engine v0.5.36

## Additional Information

* Async Mode Nginx SSL engine framework provides new directives:

**Directives**
```bash
Syntax:     ssl_asynch on | off;
Default:    ssl_asynch off;
Context:    http, server

    Enables SSL/TLS asynchronous mode
```
**Example**

file: conf/nginx.conf

```bash
    http {
        ssl_asynch  on;
        server {
            ...
            }
        }
    }
```

```bash
    http {
        server {
            ssl_asynch  on;
            }
        }
    }
```

* [White Paper: Intel&reg; Quickassist Technology and OpenSSL-1.1.0:Performance][2]

[2]: https://01.org/sites/default/files/downloads/intelr-quickassist-technology/intelquickassisttechnologyopensslperformance.pdf

## Limitations

* Nginx supports `reload` operation, when QAT hardware is involved for crypto
  offload, user should enure that there are enough number of qat instances.
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
        LimitDevAccess = 1
    ```

## Installation Instructions

### Install Async Mode Nginx

**Set the following environmental variables:**

`NGINX_INSTALL_DIR` is the directory where nginx will be installed to

`OPENSSL_LIB` is the directory where the openssl has been installed to

**Configure nginx for compilation:**

```bash
    ./configure \
        --prefix=$NGINX_INSTALL_DIR \
        --with-http_ssl_module \
        --add-dynamic-module=modules/nginx_qat_module/ \
        --with-cc-opt="-DNGX_SECURE_MEM -I$OPENSSL_LIB/include -Wno-error=deprecated-declarations" \
        --with-ld-opt="-Wl,-rpath=$OPENSSL_LIB/lib -L$OPENSSL_LIB/lib"
```

**Compile and Install:**

```bash
    make
    make install
```

### Build OpenSSL\* and QAT engine

These instructions can be found on [QAT engine][4]

[4]: https://github.com/intel/QAT_Engine#installation-instructions

## SSL Engine Framework for Configuration

As QAT engine is implemented as a standard OpenSSL\* engine, its behavior can be
controlled from the OpenSSL\* configuration file (`openssl.conf`), which can be
found on [QAT engine][5].

[5]: https://github.com/intel/QAT_Engine#using-the-openssl-configuration-file-to-loadinitialize-engines

**Note**:
From v0.3.2 and later, this kind of configuration in `openssl.conf` will not be
effective for Nginx. Please use the following method to configure Nginx SSL
engine, such as Intel&reg; QAT.

An SSL Engine Framework is introduced to provide a more powerful and flexible
mechanism to configure Nginx SSL engine direclty in the Nginx configuration file
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
Any 3rd party modules can be intergrated into this framwork. By default, a
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
The qat_module provides two kinds of nginx side polling for QAT engine,

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

**Directives in the qat_module **
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
* External polling timer is enabled by default when heruistic polling mode is enabled.

In the heuristic polling mode, a polling operation is only trigerred at a proper moment:

* Number of in-flight offload requests reaches a pre-defined threshold (in consideration of efficiency)
* All the active SSL connections have submitted their cryptographic requests and been waitting for
the corresponding responses (in consideration of timeliness).

**Directives in the qat_module **
```bash
Syntax:     qat_heuristic_poll_asym_threshold num;
Default:    32
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_engine
Description:
            Threshold of the number of in-flight asymmetric-key requests to
            trigger a polling operation
            Valid value: 1 ~ 512


Syntax:     qat_heuristic_poll_cipher_threshold num;
Default:    16
Dependency: Valid if (qat_poll_mode=heuristic)
Context:    qat_engine
Description:
            Threshold of the number of in-flight cipher requests to trigger a
            polling operation
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
            qat_heuristic_poll_asym_threshold 32;
            qat_heuristic_poll_cipher_threshold 16;
        }
    }
```


## Known issue
** SSL asynch mode is not supported in HTTP proxy and Stream module **
   If HTTP proxy and stream module are employed, QAT engine can only be configured
   in `internal polling` mode to ensure there is polling thread, or the connection
   will be blocked.

** 'Orphan ring' errors in `dmesg` output when Nginx exit **
   Working with current QAT driver (version 1.0.3 in 01.org), Nginx workers exit
   with 'Orphan ring' errors. This issue has been fixed in next QAT driver release

** Cache manager/loader process will allocate QAT instance via QAT engine **
   According to current QAT engine design, child process forked by master
   process will initialize QAT engine automatically in QAT engine `atfork`
   hook function. If cache manager/loader processes are employed, QAT instances
   will be allocated in the same way as worker process. Cache manager/loader
   processes do not perform modules' `exit process` method in Nginx native design
   which will introduce "Orphan ring" error message in `dmesg` output.


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

