/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iterator>
#include <unistd.h>
#include <sys/types.h>   
#include <sys/stat.h>
#include "web_server.hh"
#include "nginx_configuration.hh"
#include "system_runner.hh"
#include "config.h"
#include "util.hh"
#include "exception.hh"

using namespace std;

WebServer::WebServer( const Address & addr, const std::set<std::string> & servernames, const string & working_directory, const string & record_path, const std::string & keyfile, const std::string & certfile )
    : config_file_( "/tmp/replayshell_nginx_config" ),
      nginx_pid_file_("/tmp/replayshell_nginx_pidfile"),
      fastcgi_pid_file_("/tmp/replayshell_fastcgi_pidfile." + to_string(getpid())),
      nginxsrv(nullptr),
      moved_away_( false )
{
    nginx_pid_file_ = "/tmp/replayshell_nginx_pid." + to_string( getpid() ) + "." + to_string( random() );
    std::ostringstream stream;
    std::copy(servernames.begin(), servernames.end(), std::ostream_iterator<std::string>(stream, " "));
    std::string server_names = stream.str();
    if (server_names.length() > 0) {
        server_names.substr(0, server_names.length()-1);
    }
    std::cout << "certfile : " << certfile << std::endl;
    std::cout << "keyfile : " << keyfile << std::endl;

    config_file_.write( nginx_main_cfg );
    config_file_.write( "pid "+nginx_pid_file_+ ";\n" );

    config_file_.write( "quic {\n" );
        config_file_.write( "   quic_stack stack1 {\n" );
        config_file_.write( "       quic_listen "+addr.str()+";\n");
        config_file_.write( "       quic_max_streams_per_connection 88;\n");
        config_file_.write( "       quic_initial_idle_timeout_in_sec 5;\n");
        config_file_.write( "       quic_default_idle_timeout_in_sec 10;\n");
        config_file_.write( "       quic_max_idle_timeout_in_sec 10;\n");
        config_file_.write( "       quic_max_time_before_crypto_handshake_in_sec 10;\n");
        config_file_.write( "       quic_session_buffer_size 1M;\n");
        config_file_.write( "       quic_max_age 600;\n");
        config_file_.write( "       quic_parameter_server  http://127.0.0.1;\n");
        config_file_.write( "   }\n" );
    config_file_.write( "}\n" );
    
    config_file_.write( "http {\n" );
    config_file_.write( "   include       /data/tools/nginx/conf/mime.types;\n" );
    config_file_.write( "   default_type  application/octet-stream;\n" );

    config_file_.write( "   log_format  main  '$remote_addr - $remote_user [$time_local] \"$request\" '\n" );
    config_file_.write( "       '$status $body_bytes_sent \"$http_referer\" '\n" );
    config_file_.write( "       '\"$http_user_agent\" \"$http_x_forwarded_for\"';\n" );

    config_file_.write( "   access_log  logs/access.log  main;\n" );
    config_file_.write( "   error_log  logs/error_http.log debug;\n" );

    config_file_.write( "   sendfile        on;\n" );
    config_file_.write( "   #tcp_nopush     on;\n" );
    config_file_.write( "   keepalive_timeout  65;\n" );
    config_file_.write( "   #gzip  on;\n" );
    
    config_file_.write( "   server {\n" );
    if ( addr.port() != 80 ) {   
        std::cout << "WebServer init ssl, ip: " << addr.str() << std::endl;
        config_file_.write( "        listen "+addr.str()+" ssl http2;\n");
        config_file_.write( "        server_name "+server_names+";\n");
        config_file_.write( "        ssl_certificate "+certfile+";\n");
        config_file_.write( "        ssl_certificate_key "+keyfile+";\n");
        config_file_.write( "        ssl_session_timeout 30m;\n");
        config_file_.write( "        ssl_protocols TLSv1.3 TLSv1.2 TLSv1.1 TLSv1;\n");
        config_file_.write( "        ssl_ciphers EECDH+CHACHA20:EECDH+CHACHA20-draft:EECDH+ECDSA+AES128:RSA+AES128:EECDH+ECDSA+AES256:RSA+AES256:EECDH+3DES:RSA+3DES:!MD5;\n");
        config_file_.write( "        ssl_session_cache shared:SSL:50m;\n");
        config_file_.write( "        ssl_prefer_server_ciphers on;\n");

        // quic configuration
        config_file_.write( "        enable_quic stack1;\n");

    } else {
        std::cout << "NOT WebServer init ssl, ip: " << addr.str() << std::endl;
        config_file_.write( "        listen "+addr.str()+";\n");
        config_file_.write( "        server_name "+server_names+";\n");
    }

    config_file_.write( "       location / {\n" );
    config_file_.write( "           fastcgi_pass " +addr.ip()+ ":9001;\n" );
    config_file_.write( "           include /data/tools/nginx/conf/fastcgi.conf;\n" );
    config_file_.write( "           fastcgi_param  SCRIPT_FILENAME /data/mahimahi/fcgi/FcgiHandler.py;\n" );
    config_file_.write( "           fastcgi_param  NGINX_CUSTOM_SCHEDULER 0;\n" );
    config_file_.write( "           fastcgi_param  PUSH_STRATEGY_FILE noop;\n" );
    config_file_.write( "           fastcgi_param  WORKING_DIR \""+working_directory+"\";\n" );
    config_file_.write( "           fastcgi_param  RECORDING_DIR \""+record_path+"\";\n" );
    config_file_.write( "           fastcgi_param  REPLAYSERVER_FN \""+string(REPLAYSERVER)+"\";\n" );
    config_file_.write( "           add_header alt-svc 'h3-29=\":443\"; ma=900, h3-Q050=\":443\"; ma=900,h3-27=\":443\"; ma=900,h3-T051=\":443\"; ma=900,h3-T050=\":443\"; ma=900,h3-Q046=\":443\"; ma=900,h3-Q043=\":443\"; ma=900, quic=\":443\"; ma=900; v=\"46,43\"';\n" );
    config_file_.write( "       }\n" );
    
    // end of server block
    config_file_.write( "   }\n" );
    
    // end of http block
    config_file_.write( "}\n" );
    
    vector< string > args = { "/data/tools/nginx/sbin/nginx", "-c", config_file_.name() };
    run(args);

    /* set fastcgi environment */
    SystemCall( "setenv", setenv( "NGINX_CUSTOM_SCHEDULER", "0", false) );
    SystemCall( "setenv", setenv( "PUSH_STRATEGY_FILE", "noop", false) );

    chmod(fastcgi_pid_file_.name().data(), 0666);
    vector<string> fcgi_args = {"/usr/local/bin/spawn-fcgi",
                                "-a", addr.ip(),
                                "-p", "9001",
                                "-P", fastcgi_pid_file_.name(),
                                "-f", "/data/mahimahi/fcgi/FcgiHandler.py"};

    int status = system( join(fcgi_args).data() );
    if(status == -1) {
        std::cout << "system error" << std::endl;
    }
    environ = nullptr;
}

WebServer::~WebServer()
{   
    if ( moved_away_ ) { return; }
    try {
        // kill nginx thread
        vector< string > args = { "/data/tools/nginx/sbin/nginx", "-c", config_file_.name(), "-s", "stop"};
        run(args);

        // kill fcgi thread
        std::string pid = fastcgi_pid_file_.fd().read();
        if (!pid.empty())
            kill(std::stoi(pid), SIGTERM);
        return;
    } catch ( const exception & e ) { /* don't throw from destructor */
        print_exception( e );
    }
}

WebServer::WebServer( WebServer && other )
    : config_file_( move( other.config_file_ ) ),
      nginx_pid_file_( move( other.nginx_pid_file_ ) ),
      fastcgi_pid_file_( move( other.fastcgi_pid_file_ ) ),
      nginxsrv(move(other.nginxsrv)),
      moved_away_( false )
{
    other.nginxsrv = nullptr;
    other.moved_away_ = true;
}
