# Intel® QuickAssist Technology(QAT) Async Mode Nginx\* Container support

Async Mode Nginx Dockerfile contains Crypto and Compression acceleration with both QAT_HW and QAT_SW which can be built into docker images on the platforms with [Intel® QuickAssist 4xxx Series](https://www.intel.com/content/www/us/en/products/details/processors/xeon/scalable.html) QAT device.

This Dockerfile(qat_crypto_base+compression/Dockerfile) with qatengine is built and validated on top of OpenSSL-3.0.15, QAT_HW(qatlib intree driver) and QAT_SW with software versions mentioned in [software_requirements](../README.md#software_requirements) section.

## Docker setup and testing

Refer [here](https://intel.github.io/quickassist/AppNotes/Containers/setup.html)
for setting up the host for QAT_HW (qatlib intree) if the platform has QAT 4xxx Hardware
device. Stop QAT service if any running on the host.

### QAT_HW settings

Follow the below steps to enable required service. The service can be asym only, sym only or both
in step 2 depending on the particular use case. Configure the required service only to get best performance.

1. Bring down the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo down > /sys/bus/pci/devices/$i/qat/state;done
```

2. Set up the required crypto or compression service(s)
   To enable crypto service use "sym;asym"
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo "sym;asym" > /sys/bus/pci/devices/$i/qat/cfg_services;done
```
   To enable compression service use "dc" or both means "dc;sym" / "dc;asym" update accordingly in above command.

3. Bring up the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo up> /sys/bus/pci/devices/$i/qat/state;done
```

4. Check the status of the QAT devices
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do cat /sys/bus/pci/devices/$i/qat/state;done
```

5. Enable VF for the PF in the host
```
    for i in `lspci -D -d :4940| awk '{print $1}'`; do echo 16|sudo tee /sys/bus/pci/devices/$i/sriov_numvfs; done
```

6. Add QAT group and Permission to the VF devices in the host
```
    chown root.qat /dev/vfio/*
    chmod 660 /dev/vfio/*
```

### Generate certificates

Create the TLS key and certificate for enabling encryption

```
openssl genrsa -out rsa1k.key.pem 1024
openssl req -new -x509 -key rsa1k.key.pem -out rsa1k.cert.pem -days 360 -subj "/C=US/ST=State/L=Locality/O=Company/OU=Section/CN=(1024 bit RSA)/emailAddress=xxx@company.com"
```
Note: Replace <path> for the absolute path where you want to save the file(/etc/ssl/certs/).

### Image creation

Docker images can be build using the below command with appropiate image name.

```
docker build --build-arg GID=$(getent group qat | cut -d ':' -f 3) -t <docker_image_name> <path-to-dockerfile> --no-cache
```
Note: GID is the group id of qat group in the host.

### Test using Async Nginx\* crypto utility

```
Server command: docker run --rm -it --cpuset-cpus <2-n+1> --cap-add=IPC_LOCK --security-opt seccomp=unconfined --security-opt apparmor=unconfined $(for i in `ls /dev/vfio/*`; do echo --device $i; done) --env QAT_POLICY=1 --ulimit memlock=524288000:524288000 -v /usr/share/nginx/:/usr/share/nginx/ -v /etc/ssl/certs/:/etc/ssl/certs/ -v /var/www/html/:/var/www/html/ -v /var/www/logs/:/var/www/logs/ -d -p 8080:8080 <docker_image_name>

Client command: openssl  s_time -connect <server_ip>:8080 -new -cipher AES128-GCM-SHA256 -www /10mb_file.html -time 5
```
Note: n is number of process or thread. 8080 port to be used for starting the async nginx container using -v /usr/share/nginx/, /etc/ssl/certs/, /var/www/html/ and /var/www/logs/.

