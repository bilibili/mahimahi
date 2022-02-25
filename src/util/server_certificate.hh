/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef CERTIFICATE_HH
#define CERTIFICATE_HH

#include <functional>
#include <unistd.h>
#include <vector>
#include <set>
#include <string>
#include <memory>

#include "temp_file.hh"
#include "ca_environment.hh"


class ServerCertificate
{
public:
    std::unique_ptr<TempFile> certificate_file;
    std::unique_ptr<TempFile> privatekey_file;
    ServerCertificate(const std::set<std::string>& alt_names, const CAEnvironment& caenv, char* ca_key);
    ~ServerCertificate();
};

#endif /* CERTIFICATE_HH */
