# Intel BorinSSL QAT Module for Nginx

This module integrates the Intel QuickAssist Technology (QAT) engine into the Nginx framework to accelerate cryptographic operations via BoringSSL.

## Dependencies

- BoringSSL commit: 23ed9d3852bbc738bebeaa0fe4a0782f91d7873c
- QAT_Engine library v1.9.0

## Installation

1. Add the following line to `config.example`:
    --add-dynamic-module=modules/ngx_bssl_qat_module

2. Build Nginx with the module:
    ```
    ./configure --prefix=<install_dir> \
        --with-http_v3_module \
        --with-cc-opt="-I<path/to/qat_engine/sourcecode> -I<path/to/boringssl/include>" \
        --with-ld-opt="-Wl,-rpath=<path/to/boringssl/lib> -L<path/to/boringssl/lib>" \
        --add-dynamic-module=modules/ngx_bssl_qat_module
    make && make install
    ```

## Configuration

1. Load the module in `nginx.conf`:
    ```
    load_module modules/ngx_bssl_qat_module.so;
    ```

2. Configure the QAT engine using the `ssl_engine` block:
    ```
    ssl_engine {
        use_engine qatengine;
        default_algorithms RSA,EC;
        qat_engine {
            qat_offload_mode async;
            qat_notify_mode poll;
            qat_poll_mode internal;
        }
    }
    ```

> **Note:** External and heuristic QAT poll modes are not supported in BoringSSL.
> **Note:** Only RSA and ECDSA offload is supported with ECDSA disabled by default in QATEngine.
