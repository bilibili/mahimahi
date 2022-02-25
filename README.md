# bvc-mahimahi

bvc-mahimahi is a network emulation tools implemented by BVC (Bilibili Video Cloud team). The project is based on MIT mahimahi(http://mahimahi.mit.edu/). In order to support experiment using multiple HTTP protocols such as HTTP2 and HTTP3 (QUIC), BVC replaced the apache server with nginx server on the basis of the original implementation. And by dynamically configuring HTTP network modules in the nginx configuration file, it can support the recording of various websites and replaying the recorded traffic over multiple network protocols, so that will facilitate the developers to do measurement test and tune their web service performance by using the toolkit.


### Features
- Record and replay HTTP traffic
- Emulate numerous network conditions
- Use unmodified client applications

## Getting Started

### Build  


```bash
./autogen.sh
./configure
make
sudo make install
```
### Dependencies

| Name | Typical package |
| ------ | ------ |
| Protocol Buffers | protobuf-compiler, libprotobuf-dev |
| autotools | autotools-dev |
| autoreconf | dh-autoreconf |
| iptables | iptables |
| pkg-config | pkg-config |
| dnsmasq | dnsmasq-base |
| debhelper (>= 9) | debhelper |
| OpenSSL | libssl-dev, ssl-cert |
| xcb | libxcb-present-dev |
| Cairo | libcairo2-dev |
| Pango | libpango1.0-dev |
| nginx | tengine-2.3.2 |
| spawn-fcgi | spawn-fcgi-1.6.4 |
| flup | flup-1.0.2 |
| fastcgi | fcgi2 |

### Play examples
- To record traffic from www.bilibili.com in Chromium:

```bash
cd mahimahi
mm-webrecord  test
/path-to/Chromium  --ignore-certificate-errors  --ignore-ssl-errors https://www.bilibili.com 
```
Then you will enter bilibili homepage. You can look through the web pages and click any interested item like video. The toolkit will record all the web HTTP traffic during the whole process until you type ‘exit’ to quit the shell.
```bash
exit
```

- Pre-works for web replay:

Before excuting the replay binary, you should firstly compile and install nginx with its network modules by configuration.  In the example, the nginx version that we use is tengine-2.3.2, and the nginx QUIC network module is implemented by our team.
```bash
cd tengine-2.3.2
./configure --prefix=/data/tools/nginx --with-http_ssl_module --with-http_v2_module --add-dynamic-module=/path-to/nginx-quic-module
make && make install
```
You should also create a local root certificate authority (CA) for traffic replaying over HTTP2 protocol.
Create the root CA directory:
```bash
mkdir  /data/CA
cd /data/CA
mkdir certs crl newcerts private
chmod 700 private
touch index.txt
echo 1000 > serial.txt
```
Generate the private key of the root CA:
```bash
openssl genrsa -aes256 -out /data/CA/private/ca.key.pem 4096
    Enter pass phrase for ca.key.pem:SECRET
    Verifying - Enter pass phrase for ca.key.pem:SECRET
chmod 400 /data/CA/private/ca.key.pem
```
Create the root CA config file:
```bash
cd  /data/CA
cp /etc/ssl/openssl.cnf root_CA.cnf
vim root_CA.cnf
```
Edit CA config template to ensure the following fields to be properly set:
```bash
    [ CA_default ]

        dir = /path-to/mahimahi/CA
        certs = $dir/certs
        private = $dir/private
        certificate = $certs/ca.cert.pem
        private_key = $private/ca.key.pem

    [ usr_cert ]
        basicConstraints=CA:FALSE
        keyUsage = nonRepudiation, digitalSignature, keyEncipherment
        nsComment = "OpenSSL Generated Certificate"
        subjectKeyIdentifier=hash
        authorityKeyIdentifier=keyid,issuer

    [ v3_ca ]
        subjectKeyIdentifier=hash
        authorityKeyIdentifier=keyid:always,issuer
        basicConstraints = CA:true
        keyUsage = cRLSign, keyCertSign
```
Generate the self-signed root CA certificate:
```bash
cd  /data/CA
openssl req -new -x509 -days 3650 -key private/ca.key.pem -sha256 -extensions v3_ca -out certs/ca.cert.pem -config root_CA.cnf
```
The root CA certificate will be generated and saved in certs/ directory, and will be later used to generate self-signed certificates for each individual web server.

- To replay traffic over HTTP2 protocol in Chromium:

Run webreplay binary and input secret in order to generate certificates to be used by nginx server.
```bash
cd mahimahi
mm-webreplay  test
```
Install self-signed root CA to Chromium:
Open Chromium → settings → Privacy and security → security → Manage certificates → Authorities → import /data/CA/cert/ca.cert.pem → restart Chromium
```bash
/path-to/Chromium https://www.bilibili.com
```

- To replay traffic over QUIC protocol in Chromium:

Because Chromium handles QUIC and TLS (over TCP) differently in cert verification, we should explicitly set some flags to force traffic run over QUIC. Especially, we shoud explicitly set hostname and QUIC version for those servers we expect them to server over QUIC, while the rest will serve over HTTP2.
```bash
 /path-to/Chromium --quic-host-whitelist=<sitename> --origin-to-force-quic-on=<sitename:port> --quic-version=h3-Q050  https://www.bilibili.com
```
