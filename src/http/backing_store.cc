/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <iostream>
#include <string>

#include "util.hh"
#include "backing_store.hh"
#include "http_record.pb.h"
#include "temp_file.hh"

using namespace std;

HTTPDiskStore::HTTPDiskStore( const string & record_folder )
    : record_folder_( record_folder ),
      mutex_()
{}

void HTTPDiskStore::save( const HTTPResponse & response, const Address & server_address )
{
    unique_lock<mutex> ul( mutex_ );

    /* output file to write current request/response pair protobuf (user has all permissions) */
    //UniqueFile file( record_folder_ + "save" );
    std::cout << "before stripped: " << response.request().first_line() << std::endl;
    auto stripped = strip_query(response.request().first_line());
    std::cout << "after stripped: " << stripped << std::endl;
    string hash = to_string(hash32(reinterpret_cast<const uint8_t*>(stripped.c_str()),stripped.size()));
    UniqueFile file( record_folder_ + hash );
     std::cout << "hash: " << hash << std::endl;
    /* construct protocol buffer */
    MahimahiProtobufs::RequestResponse output;

    output.set_ip( server_address.ip() );
    output.set_port( server_address.port() );
    //output.set_scheme( (server_address.port() == 443)
    output.set_scheme( (server_address.port() != 80)
                       ? MahimahiProtobufs::RequestResponse_Scheme_HTTPS
                       : MahimahiProtobufs::RequestResponse_Scheme_HTTP );
    std::cout << "save ip to disk: " << server_address.str() << std::endl;
    output.mutable_request()->CopyFrom( response.request().toprotobuf() );
    output.mutable_response()->CopyFrom( response.toprotobuf() );

    if ( not output.SerializeToFileDescriptor( file.fd().fd_num() ) ) {
        throw runtime_error( "save_to_disk: failure to serialize HTTP request/response pair" );
    }

}
