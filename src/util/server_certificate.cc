/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include "server_certificate.hh"
#include "system_runner.hh"
#include "exception.hh"

#include <string>
#include <set>
#include <fcntl.h>
#include "util.hh"

using namespace std;

ServerCertificate::ServerCertificate(const std::set<std::string>& alt_names, const CAEnvironment& caenv, char* ca_key)
:certificate_file(nullptr),privatekey_file(nullptr)
{
    TemporarilyUnprivileged tu;
    const std::string CA_PATH = caenv.dirname;

    certificate_file = std::move(std::unique_ptr<TempFile>(new TempFile("/tmp/certificate")));
    privatekey_file  = std::move(std::unique_ptr<TempFile>(new TempFile("/tmp/private")));

    std::string ca_config = 
R"END(
HOME            = .
RANDFILE        = $ENV::HOME/.rnd

####################################################################
[ ca ]
default_ca  = CA_default        # The default ca section

[ CA_default ]

default_days    = 1000          # how long to certify for
default_crl_days= 30            # how long before next CRL
default_md  = sha256        # use public key default MD
preserve    = no            # keep passed DN ordering

base_dir    = )END"+CA_PATH+R"END(
certificate = $base_dir/cacert.pem  # The CA certifcate
private_key = $base_dir/cakey.pem   # The CA private key
new_certs_dir   = $base_dir     # Location for new certs after signing
database    = $base_dir/index.txt   # Database index file
serial      = $base_dir/serial.txt  # The current serial number

unique_subject  = no            # Set to 'no' to allow creation of
                # several certificates with same subject.



x509_extensions = ca_extensions     # The extensions to add to the cert

email_in_dn = no            # Don't concat the email in the DN
copy_extensions = copy          # Required to copy SANs from CSR to cert

####################################################################
[ req ]
default_bits        = 4096
default_keyfile     = cakey.pem
distinguished_name  = ca_distinguished_name
x509_extensions     = ca_extensions
string_mask         = utf8only

####################################################################
[ ca_distinguished_name ]
countryName         = Country Name (2 letter code)
countryName_default     = US

stateOrProvinceName     = State or Province Name (full name)
stateOrProvinceName_default = Maryland

localityName            = Locality Name (eg, city)
localityName_default        = Baltimore

organizationName            = Organization Name (eg, company)
organizationName_default    = Test CA, Limited

organizationalUnitName  = Organizational Unit (eg, division)
organizationalUnitName_default  = Server Research Department

commonName          = Common Name (e.g. server FQDN or YOUR name)
commonName_default      = Test CA

emailAddress            = Email Address
emailAddress_default        = test@example.com

####################################################################
[ ca_extensions ]

subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid:always, issuer
basicConstraints = critical, CA:true
keyUsage = keyCertSign, cRLSign


####################################################################
[ signing_policy ]
countryName     = optional
stateOrProvinceName = optional
localityName        = optional
organizationName    = optional
organizationalUnitName  = optional
commonName      = supplied
emailAddress        = optional

####################################################################
[ signing_req ]
subjectKeyIdentifier=hash
authorityKeyIdentifier=keyid,issuer

basicConstraints = CA:FALSE
keyUsage = digitalSignature, keyEncipherment
)END";
    
    std::string config = 
R"END(
HOME            = .
RANDFILE        = $ENV::HOME/.rnd

####################################################################
[ req ]
default_bits        = 2048
default_keyfile     = )END"+privatekey_file->name()+R"END(
distinguished_name  = server_distinguished_name
req_extensions      = server_req_extensions
string_mask         = utf8only

####################################################################
[ server_distinguished_name ]
countryName         = Country Name (2 letter code)
countryName_default     = US

stateOrProvinceName     = State or Province Name (full name)
stateOrProvinceName_default = MD

localityName            = Locality Name (eg, city)
localityName_default        = Baltimore

organizationName            = Organization Name (eg, company)
organizationName_default    = Test CA, Limited

commonName          = Common Name (e.g. server FQDN or YOUR name)
commonName_default      = Test CA

emailAddress            = Email Address
emailAddress_default        = test@example.com

####################################################################
[ server_req_extensions ]

subjectKeyIdentifier        = hash
basicConstraints        = CA:FALSE
keyUsage            = digitalSignature, keyEncipherment
subjectAltName          = @alternate_names
nsComment           = "OpenSSL Generated Certificate"

####################################################################
[ alternate_names ]

)END";

int i=1;
for(auto & alt_name: alt_names) 
{
    std::cout << "ALT NAME " << alt_name << std::endl;
    config += "DNS."+std::to_string(i)+"       = "+alt_name+"\n";
    i++;
}

TempFile configfile("/tmp/csrcfg");
TempFile caconfig("/tmp/cacfg");
caconfig.write(ca_config);
configfile.write(config);

TempFile csr("/tmp/csr");

vector< string > args_req = {"/usr/bin/openssl", "req", "-batch", "-config",configfile.name(), 
                             "-newkey","rsa:2048","-sha256","-nodes", "-out",csr.name(),"-outform","PEM"};
run ( args_req );

std::string key = ca_key;
vector< string > args_ca = {"/usr/bin/openssl", "ca", "-batch", "-config", caconfig.name(), "-policy",
                            "signing_policy","-extensions","signing_req", "-passin", "pass:" + key, "-out", 
                             certificate_file->name(), "-infiles", csr.name()};
run ( args_ca );
}

ServerCertificate::~ServerCertificate()
{}
