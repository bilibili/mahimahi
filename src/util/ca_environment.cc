/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#include "server_certificate.hh"
#include "system_runner.hh"
#include "exception.hh"

#include <string>
#include <set>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>


#include <istream>
#include <iostream>
#include <fstream>
#include <iterator>

#include "ca_environment.hh"
#include "util.hh"

using namespace std;

void CAEnvironment::copyFile(std::string from, std::string to)
{
    std::ifstream srce( from, std::ios::binary ) ;
    std::ofstream dest( to, std::ios::binary ) ;
    dest << srce.rdbuf() ;
}

CAEnvironment::CAEnvironment(const string mahimahi_root)
:dirname("/tmp/nonexistent")
{
    TemporarilyUnprivileged tu;
    const std::string CA_PATH = mahimahi_root + "/CA";
    char tpl[] = "/tmp/tmp_ca.XXXXXX";
    this->dirname = mkdtemp(tpl);

    copyFile(CA_PATH+"/index.txt",this->dirname+"/index.txt");
    copyFile(CA_PATH+"/certs/ca.cert.pem",this->dirname+"/cacert.pem");
    copyFile(CA_PATH+"/private/ca.key.pem",this->dirname+"/cakey.pem");
    copyFile(CA_PATH+"/serial.txt",this->dirname+"/serial.txt");
}

CAEnvironment::~CAEnvironment()
{
    DIR *theFolder = opendir(this->dirname.c_str());
    struct dirent *next_file;
    char filepath[1024];

    while ( (next_file = readdir(theFolder)) != NULL )
    {
        if (0==strcmp(next_file->d_name, ".") || 0==strcmp(next_file->d_name, "..")) { continue; }
        // build the path for each file in the folder
        snprintf(filepath, sizeof(filepath), "%s/%s", this->dirname.c_str(), next_file->d_name);
        remove(filepath);
    }
    closedir(theFolder);
    if(rmdir(this->dirname.c_str()) == -1)
    {
            perror("rmdir failed: ");
    }
}
