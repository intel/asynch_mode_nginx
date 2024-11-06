#==========================================================================
#                                                                         \
#                                                                         \
#   BSD LICENSE                                                           \
#                                                                         \
#   Copyright(c) 2024 Intel Corporation.                                  \
#   All rights reserved.                                                  \
#                                                                         \
#   Redistribution and use in source and binary forms, with or without    \
#   modification, are permitted provided that the following conditions    \
#   are met:                                                              \
#                                                                         \
#     * Redistributions of source code must retain the above copyright    \
#       notice, this list of conditions and the following disclaimer.     \
#     * Redistributions in binary form must reproduce the above copyright \
#       notice, this list of conditions and the following disclaimer in   \
#       the documentation and/or other materials provided with the        \
#       distribution.                                                     \
#     * Neither the name of Intel Corporation nor the names of its        \
#       contributors may be used to endorse or promote products derived   \
#       from this software without specific prior written permission.     \
#                                                                         \
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS   \
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT     \
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR \
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  \
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, \
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT      \
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, \
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY \
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT   \
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE \
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  \
#                                                                         \
#                                                                         \
#==========================================================================

ARG UBUNTU_BASE=ubuntu:22.04
FROM ${UBUNTU_BASE} AS builder

ARG OPENSSL_VERSION="openssl-3.0.15"
ARG QATLIB_VERSION="24.02.0"
ARG QAT_ENGINE_VERSION="v1.6.2"
ARG IPSEC_MB_VERSION="v1.5"
ARG IPP_CRYPTO_VERSION="ippcp_2021.12.1"
ARG QATZIP_VERSION="v1.2.0"
ARG ASYNC_NGINX_VERSION="v0.5.2"
ARG GID
ENV DEBIAN_FRONTEND=noninteractive

# Install required packages
RUN apt-get update && \
    apt-get install -y apt-utils

# Upgrade all other packages
RUN apt-get upgrade -y && \
    apt-get install -y \
    libudev-dev \
    make \
    gcc \
    g++ \
    nasm \
    pkg-config \
    libssl-dev \
    libpcre3-dev \
    zlib1g-dev \
    libreadline-dev \
    lua5.4 \
    liblua5.4-dev \
    nasm \
    autoconf \
    automake \
    cmake \
    git \
    ca-certificates \
    liblz4-dev \
    libtool && \
    git clone --depth 1 -b $OPENSSL_VERSION https://github.com/openssl/openssl.git && \
    git clone --depth 1 -b $QAT_ENGINE_VERSION https://github.com/intel/QAT_Engine && \
    git clone --depth 1 -b $IPP_CRYPTO_VERSION https://github.com/intel/ipp-crypto && \
    git clone --depth 1 -b $IPSEC_MB_VERSION https://github.com/intel/intel-ipsec-mb && \
    git clone --depth 1 -b $QATLIB_VERSION https://github.com/intel/qatlib && \
    git clone --depth 1 -b $QATZIP_VERSION https://github.com/intel/QATzip.git && \
    git clone --depth 1 -b $ASYNC_NGINX_VERSION https://github.com/intel/asynch_mode_nginx

# Create a non-root user and group
RUN groupadd -r appuser && useradd -r -g appuser -s /bin/bash appuser

# Build and Install OpenSSL
WORKDIR /openssl
RUN ./config && \
    make -j && \
    make install -j

# Build and Install QATLib
WORKDIR /qatlib
RUN ./autogen.sh && \
    ./configure --enable-systemd=no && \
    make -j && \
    make install samples-install && \
    groupadd qat -g ${GID} && \
    usermod -a -G qat appuser

# Build and Install Crypto-MB
WORKDIR /ipp-crypto/sources/ippcp/crypto_mb
RUN cmake . -B"../build" \
    -DOPENSSL_INCLUDE_DIR=/usr/local/include \
    -DOPENSSL_LIBRARIES=/usr/local/lib64 \
    -DOPENSSL_ROOT_DIR=/openssl

WORKDIR /ipp-crypto/sources/ippcp/build
RUN make crypto_mb -j && make install -j

# Building the Ipsec-MB
WORKDIR /intel-ipsec-mb
RUN make -j && make install LIB_INSTALL_DIR=/usr/local/lib

# Build & Install QATEngine
WORKDIR /QAT_Engine
RUN ./autogen.sh && \
    ./configure \
    --with-openssl_install_dir=/usr/local/ \
    --with-qat-hw-dir=/usr/local/ \
    --enable-qat_sw && \
    make -j && make install -j

# Build & Install QATzip
WORKDIR /QATzip
RUN ./autogen.sh && \
    ./configure && \
    make -j && make install -j

# Build & Install Asynch_mode_nginx
WORKDIR /asynch_mode_nginx
RUN ./configure \
    --prefix=/var/www \
    --conf-path=/usr/share/nginx/conf/nginx.conf \
    --sbin-path=/usr/bin/ \
    --with-http_ssl_module \
    --with-http_stub_status_module \
    --add-dynamic-module=modules/nginx_qatzip_module \
    --add-dynamic-module=modules/nginx_qat_module/ \
    --with-cc-opt="-DNGX_SECURE_MEM -I/usr/local/include -I/usr/local/include/qat -Wno-error=deprecated-declarations" \
    --with-ld-opt="-Wl,-rpath=/usr/local/lib64 -L/usr/local/lib64 -L/QATzip/src -lqatzip -lz" && \
    make -j && \
    make install -j

#Added to remove libc library for vulnerability issue
RUN apt-get purge -y linux-libc-dev

FROM ${UBUNTU_BASE}

COPY --from=builder /usr/local/lib/libqat.so.4.2.0 /usr/lib/
COPY --from=builder /usr/local/lib/libusdm.so.0.1.0 /usr/lib/
COPY --from=builder /usr/local/lib/libIPSec_MB.so.1.5.0 /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/local/lib64/libcrypto.so.3 /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/local/lib/libcrypto_mb.so.11.15 /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/local/bin/openssl /usr/bin/
COPY --from=builder /usr/local/lib64/engines-3/qatengine.so /usr/lib/x86_64-linux-gnu/engines-3/qatengine.so
COPY --from=builder /var/www/ /var/www/
COPY --from=builder /usr/bin/nginx /usr/bin/nginx
COPY --from=builder /usr/share/nginx/conf/* /usr/share/nginx/conf/
COPY --from=builder /usr/share/nginx/conf/nginx.conf /usr/share/nginx/conf/nginx.conf
COPY --from=builder /usr/local/lib/libqatzip.so.3.0.3 /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/local/bin/qzip /usr/bin/qzip
COPY --from=builder /usr/local/bin/qatzip-test /usr/bin/qatzip-test
COPY --from=builder /etc/group /etc/group
COPY --from=builder /etc/passwd /etc/passwd
RUN touch /var/www/logs/error.log /var/www/logs/nginx.pid /var/www/logs/access.log && chown appuser /var/www/logs/error.log /var/www/logs/nginx.pid /var/www/logs/access.log

RUN ldconfig

# Create necessary directories and set permissions
RUN chown -R appuser:appuser /usr/share/nginx /var/www/ /usr/bin/nginx /usr/bin/qzip && \
    chmod -R 777 /usr/share/nginx /var/www/ /usr/bin/nginx /usr/bin/qzip

RUN mkdir -p /var/www/client_body_temp /var/www/proxy_temp /var/www/scgi_temp /var/www/uwsgi_temp /var/www/fastcgi_temp /var/www/html/basic_status && \
    chown -R appuser:appuser /var/www/client_body_temp /var/www/proxy_temp /var/www/scgi_temp /var/www/uwsgi_temp /var/www/fastcgi_temp /var/www/html/basic_status && \
    chmod 755 /var/www/client_body_temp /var/www/proxy_temp /var/www/scgi_temp /var/www/uwsgi_temp /var/www/fastcgi_temp /var/www/html/basic_status

#Switch to non-root user
USER appuser

ENV OPENSSL_ENGINES="/usr/lib/x86_64-linux-gnu/engines-3/"
ENV LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu/"
ENV QAT_POLICY=1

#Expose ports
EXPOSE 8080 

CMD ["/usr/bin/nginx", "-g", "daemon off;"]
