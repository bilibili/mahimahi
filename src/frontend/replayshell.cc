/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <net/route.h>
#include <fcntl.h>

#include <vector>
#include <set>

#include "util.hh"
#include "netdevice.hh"
#include "web_server.hh"
#include "system_runner.hh"
#include "socket.hh"
#include "event_loop.hh"
#include "temp_file.hh"
#include "http_response.hh"
#include "dns_server.hh"
#include "exception.hh"
#include "server_certificate.hh"
#include "nginx_configuration.hh"

#include "http_record.pb.h"

#include "config.h"

using namespace std;

void add_dummy_interface( const string & name, const Address & addr )
{
    run( { IP, "link", "add", name, "type", "dummy" } );

    interface_ioctl( SIOCSIFFLAGS, name,
                     [] ( ifreq &ifr ) { ifr.ifr_flags = IFF_UP; } );
    interface_ioctl( SIOCSIFADDR, name,
                     [&] ( ifreq &ifr ) { ifr.ifr_addr = addr.to_sockaddr(); } );
}

int main( int argc, char *argv[] )
{
    try {
        /* clear environment */
        char **user_environment = environ;
        environ = nullptr;

        check_requirements( argc, argv );

        if ( argc < 2 ) {
            throw runtime_error( "Usage: " + string( argv[ 0 ] ) + " directory [command...]" );
        }

        /* clean directory name */
        string directory = argv[ 1 ];

        if ( directory.empty() ) {
            throw runtime_error( string( argv[ 0 ] ) + ": directory name must be non-empty" );
        }

        /* make sure directory ends with '/' so we can prepend directory to file name for storage */
        if ( directory.back() != '/' ) {
            directory.append( "/" );
        }

        /* get working directory */
        const string working_directory { get_working_directory() };

        /* chdir to result of getcwd just in case */
        SystemCall( "chdir", chdir( working_directory.c_str() ) );

        /* what command will we run inside the container? */
        vector< string > command;
        if ( argc == 2 ) {
            command.push_back( shell_path() );
        } else {
            for ( int i = 2; i < argc; i++ ) {
                command.push_back( argv[ i ] );
            }
        }

        /* create a new network namespace */
        SystemCall( "unshare", unshare( CLONE_NEWNET ) );

        /* bring up localhost */
        interface_ioctl( SIOCSIFFLAGS, "lo",
                         [] ( ifreq &ifr ) { ifr.ifr_flags = IFF_UP; } );

        /* provide seed for random number generator used to create apache pid files */
        srandom( time( NULL ) );

        /* collect the IPs, IPs and ports, and hostnames we'll need to serve */
        set< Address > unique_ip;
        set< Address > unique_ip_and_port;
        vector< pair< string, Address > > hostname_to_ip;

        {
            TemporarilyUnprivileged tu;
            /* would be privilege escalation if we let the user read directories or open files as root */

            const vector< string > files = list_directory_contents( directory  );

            for ( const auto filename : files ) {
                FileDescriptor fd( SystemCall( "open", open( filename.c_str(), O_RDONLY ) ) );

                MahimahiProtobufs::RequestResponse protobuf;
                if ( not protobuf.ParseFromFileDescriptor( fd.fd_num() ) ) {
                    throw runtime_error( filename + ": invalid HTTP request/response" );
                }

                const Address address( protobuf.ip(), protobuf.port() );

                unique_ip.emplace( address.ip(), 0 );
                unique_ip_and_port.emplace( address );

                int pos = 0;
                std::string hostname;
                hostname = HTTPRequest( protobuf.request() ).get_header_value( "Host" );
                pos = hostname.find(":");
                hostname_to_ip.emplace_back( hostname.substr(0, pos), address );
            }
        }

        /* set up dummy interfaces */
        unsigned int interface_counter = 0;
        for ( const auto ip : unique_ip ) {
            add_dummy_interface( "sharded" + to_string( interface_counter ), ip );
            interface_counter++;
        }

        std::cout << "mahimahi replay working_directory : " <<working_directory << std::endl;
        CAEnvironment caenv("/data");
        /* set up web servers */
        vector< std::unique_ptr<ServerCertificate> > certificates;
        vector< WebServer > servers;
        std::set<std::string> servernames;
        string server_name;
        char* ca_key;
        ca_key = getpass ("enter priavet_key password : ");
        for ( const auto ip_port : unique_ip_and_port ) {
	    servernames.clear();
            for ( const auto mapping : hostname_to_ip ) {
                if (mapping.second== ip_port) {
                    servernames.insert(mapping.first);
                }
            }
            /* generate self-signed certificates */
	    std::unique_ptr<ServerCertificate> p1(new ServerCertificate(servernames, caenv, ca_key));
	    std::string keyfile = p1->privatekey_file->name();
	    std::string certfile = p1->certificate_file->name();
            certificates.emplace_back(std::move(p1));

	    servers.emplace_back( ip_port, servernames, working_directory, directory, keyfile, certfile);
        }

        /* set up DNS server */
        TempFile dnsmasq_hosts( "/tmp/replayshell_hosts" );
        for ( const auto mapping : hostname_to_ip ) {
            std::cout << "dns mapping: " << mapping.second.ip() << " " << mapping.first << std::endl;
            dnsmasq_hosts.write( mapping.second.ip() + " " + mapping.first + "\n" );
        }

        /* initialize event loop */
        EventLoop event_loop;

        /* create dummy interface for each nameserver */
        vector< Address > nameservers = all_nameservers();
        vector< string > dnsmasq_args = { "-H", dnsmasq_hosts.name() };

        // DNS service
        for ( unsigned int server_num = 0; server_num < nameservers.size(); server_num++ ) {
            const string interface_name = "nameserver" + to_string( server_num );
            std::cout << "add_dummy_interface: interface name: " << interface_name << ", " << nameservers.at( server_num ).str() << std::endl;
            add_dummy_interface( interface_name, nameservers.at( server_num ) );
        }

        /* start dnsmasq */
        event_loop.add_child_process( start_dnsmasq( dnsmasq_args ) );

        /* start shell */
        event_loop.add_child_process( join( command ), [&]() {
                drop_privileges();

                /* restore environment and tweak bash prompt */
                environ = user_environment;
                prepend_shell_prefix( "[replay] " );

                return ezexec( command, true );
        } );

        return event_loop.loop();
    } catch ( const exception & e ) {
        print_exception( e );
        return EXIT_FAILURE;
    }
}
