/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <string>

#include "config.h"

#ifndef NGINX_CONFIGURATION_HH
#define NGINX_CONFIGURATION_HH

const std::string nginx_main_cfg = R"END(
load_module "/data/tools/nginx/modules/ngx_quic_module.so";

#user  root;
worker_processes  1;
daemon on;

worker_rlimit_core 8000m;
working_directory /data/tools/nginx;

error_log  logs/error.log debug;
#error_log  logs/error.log;
#error_log  logs/error.log  notice;
#error_log  logs/error.log  info;
#error_log  "pipe:rollback logs/error_log interval=1d baknum=7 maxsize=2G";

#pid        logs/nginx.pid;


events {
    worker_connections  1024;
    accept_mutex off;
}

)END";
//pid-file: status/pid-file

const std::string nginx_ssl_config_certfile =std::string( MOD_SSL_CERTIFICATE_FILE );
const std::string nginx_ssl_config_keyfile =std::string( MOD_SSL_KEY );

#endif /* NGINX_CONFIGURATION_HH */
