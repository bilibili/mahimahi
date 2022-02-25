/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <vector>
#include <limits>

#include "util.hh"
#include "http_record.pb.h"
#include "http_header.hh"
#include "exception.hh"
#include "http_request.hh"
#include "http_response.hh"
#include "file_descriptor.hh"


using namespace std;

string safe_getenv( const string & key )
{
    const char * const value = getenv( key.c_str() );
    if ( not value ) {
        throw runtime_error( "missing environment variable: " + key );
    }
    return value;
}

/* does the actual HTTP header match this stored request? */
bool header_match( const string & env_var_name,
                   const string & header_name,
                   const HTTPRequest & saved_request )
{
    const char * const env_value = getenv( env_var_name.c_str() );

    /* case 1: neither header exists (OK) */
    if ( (not env_value) and (not saved_request.has_header( header_name )) ) {
        return true;
    }

    /* case 2: headers both exist (OK if values match) */
    if ( env_value and saved_request.has_header( header_name ) ) {
        return  saved_request.get_header_value( header_name).find(env_value) != string::npos;
    }

    /* case 3: one exists but the other doesn't (failure) */
    return false;
}

/* compare request_line and certain headers of incoming request and stored request */
unsigned int match_score( const MahimahiProtobufs::RequestResponse & saved_record,
                          const string & request_line,
                          const bool is_https,
                          std::string &error )
{
    const HTTPRequest saved_request( saved_record.request() );
    bool options = false;
    if (request_line.find("OPTIONS") != string::npos) {
        options = true;
    }
    /* match HTTP/HTTPS */
    if ( is_https and (saved_record.scheme() != MahimahiProtobufs::RequestResponse_Scheme_HTTPS) ) {
        error = "schema not matched";
        return 0;
    }

    if ( (not is_https) and (saved_record.scheme() != MahimahiProtobufs::RequestResponse_Scheme_HTTP) ) {
        error = "schema not matched";
        return 0;
    }

    /* match host header */
    if ( saved_request.has_header("Host") ) {
        if ( not header_match( "HTTP_HOST", "Host", saved_request ) ) {
            error = "host not matched";
            return 0;
        }
    }
    else if ( saved_request.has_header(":authority"))
    {
        const char * const env_value = getenv( "HTTP_HOST" );
        if (saved_request.get_header_value( ":authority" ) != string( env_value ))
        {
            error = std::string("authority not matched ") + env_value + " <-> " +saved_request.get_header_value( ":authority" );
            return 0;
        }
    }
    else
    {
        error = "authority, origin or host not set";
        return 0;
    }

    /* match range (ignore range if method is OPTIONS) */
    if (!options) {
        if (saved_request.has_header("Range")) {
            if ( not header_match( "HTTP_RANGE", "Range", saved_request ) ) {
                error = "range_mismatch";
                return 0;
            }
        }
    }
    /* must match first line up to "?" at least */
    if ( strip_query( request_line ) != strip_query( saved_request.first_line() ) ) {
        error = "prefix_mismatch";
        return 0;
    }

    /* success! return size of common prefix */
    const auto max_match = min( request_line.size(), saved_request.first_line().size() );
    for ( unsigned int i = 0; i < max_match; i++ ) {
        if ( request_line.at( i ) != saved_request.first_line().at( i ) ) {
            return i;
        }
    }

    return max_match;
}

int main( void )
{
    try {
        assert_not_root();
        const string working_directory = safe_getenv( "MAHIMAHI_CHDIR" );
        const string recording_directory = safe_getenv( "MAHIMAHI_RECORD_PATH" );
        const string request_line = safe_getenv( "REQUEST_METHOD" )
            + " " + safe_getenv( "REQUEST_URI" )
            + " " + safe_getenv( "SERVER_PROTOCOL" );
        const bool is_https = getenv( "HTTPS" );

        SystemCall( "chdir", chdir( working_directory.c_str() ) );

        const vector< string > files = list_directory_contents( recording_directory );

        unsigned int best_score = 0;
        std::vector<MahimahiProtobufs::RequestResponse> best_matches;

        auto stripped = strip_query(request_line);
        string hash = to_string(hash32(reinterpret_cast<const uint8_t*>(stripped.c_str()),stripped.size()));

        int num_files = 0;
        std::vector<std::string> errors;

        for ( const auto & filename : files ) {

            shared_ptr<char> ts1(strdup(filename.c_str()));
            string basefilename = string(basename(ts1.get()));
            auto cutted = basefilename.substr(0,min(hash.size(),basefilename.size()));
            if(cutted != hash)
            {
                continue;
            }

            num_files++;

            FileDescriptor fd( SystemCall( "open", open( filename.c_str(), O_RDONLY ) ) );
            MahimahiProtobufs::RequestResponse current_record;
            if ( not current_record.ParseFromFileDescriptor( fd.fd_num() ) ) {
                throw runtime_error( filename + ": invalid HTTP request/response" );
            }

            std::string error;
            unsigned int score = match_score( current_record, request_line, is_https, error);
            errors.push_back(error);
            if ( score > best_score ) {
                best_matches.clear();
                best_matches.push_back(current_record);
                best_score = score;
            }
            else if (score == best_score)
            {
                best_matches.push_back(current_record);
            }
        }

        if ( best_score > 0 ) { /* give client the best match */
            cout << HTTPResponse( best_matches[0].response() ).str();
            return EXIT_SUCCESS;
        } else {                /* no acceptable matches for request */
            cout << "HTTP/1.1 404 Not Found" << CRLF;
            cout << "Content-Type: text/plain" << CRLF << CRLF;
            cout << "Hash:" << hash << CRLF;
            for(auto error : errors)
            {
                cout << "Error:" << error << CRLF;
            }
            cout << "replayserver: could not find a match for " << request_line << CRLF;
            cout << "Files considered:  " << num_files << CRLF;
            return EXIT_FAILURE;
        }
    } catch ( const exception & e ) {
        cout << "HTTP/1.1 500 Internal Server Error" << CRLF;
        cout << "Content-Type: text/plain" << CRLF << CRLF;
        cout << "mahimahi mm-webreplay received an exception:" << CRLF << CRLF;
        print_exception( e, cout );
        return EXIT_FAILURE;
    }
}
