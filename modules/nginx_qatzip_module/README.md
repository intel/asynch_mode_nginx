# Intel QATzip Module for Nginx

This module integrates the QATZip into the Nginx framework to accelerate GZIP compression.

## Installation

1. Add the following line to `config.example`:
    ```
    --add-dynamic-module=modules/nginx_qatzip_module
    ```
2. Build Nginx with the added module:
    ```
    ./config.example
    make && make install
    ```

## Configuration

1. Load the module in `nginx.conf`:
    ```
    load_module modules/ngx_http_qatzip_filter_module.so;
    ```

## Directives

- **qatzip_sw**
  *Syntax:* `qatzip_sw only | failover | no;`
  *Default:* `qatzip_sw failover;`
  *Context:* http, server, location, if in location
  *Description:*
     - `only`: QATzip disabled
     - `failover`: QATzip enabled with software fallback
     - `no`: QATzip enabled without software fallback

- **qatzip_chunk_size**
  *Syntax:* `qatzip_chunk_size <size>;`
  *Default:* `qatzip_chunk_size 64k;`
  *Context:* http, server, location
  *Description:* Sets the chunk buffer size for compression (16K–128K, default 64K).

- **qatzip_stream_size**
  *Syntax:* `qatzip_stream_size <size>;`
  *Default:* `qatzip_stream_size 256k;`
  *Context:* http, server, location
  *Description:* Sets the stream buffer size for multiple deflate blocks (default 256K).

- **qatzip_sw_threshold**
  *Syntax:* `qatzip_sw_threshold <size>;`
  *Default:* `qatzip_sw_threshold 1k;`
  *Context:* http, server, location
  *Description:* Threshold for switching between software and QATzip compression (default 1K).

- **qatzip_min_length**
  *Syntax:* `qatzip_min_length <length>;`
  *Default:* `qatzip_min_length 20;`
  *Context:* http, server, location
  *Description:* Minimum response length for QATzip compression, based on `Content-Length`.

- **qatzip_comp_level**
  *Syntax:* `qatzip_comp_level <level>;`
  *Default:* `qatzip_comp_level 1;`
  *Context:* http, server, location
  *Description:* Compression level (1–9).

- **qatzip_buffers**
  *Syntax:* `qatzip_buffers <number> <size>;`
  *Default:* `qatzip_buffers 32 4k` or `16 8k;`
  *Context:* http, server, location
  *Description:* Number and size of buffers for compression (default: one memory page, 4K or 8K depending on platform).
