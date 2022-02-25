from StringIO import StringIO
from cgi import escape
import sys
import json
import os
import subprocess
from strip_chunked import decode
from mimetools import Message
from urlparse import urlparse
import random
import zlib
import string

class ReplayApp:

    def _parse_push_strategy(self):

        if self.push_strategy_file == "noop":
            return

        with open(self.push_strategy_file) as fd:
            parsed_push_strategy = json.load(fd)
            if len(parsed_push_strategy['push_configs']) > 0:
                for push_strategy_for_host in parsed_push_strategy['push_configs']:
                    if 'push_host' in push_strategy_for_host.keys():
                        self.push_host.append(push_strategy_for_host['push_host'])
                        self.push_trigger_path.append(push_strategy_for_host['push_trigger'])
                        self.push_assets.append(push_strategy_for_host['push_resources'])

                    if 'hint_host' in push_strategy_for_host.keys():
                        self.hint_host.append(push_strategy_for_host['hint_host'])
                        self.hint_trigger_path.append(push_strategy_for_host['hint_trigger'])
                        self.hint_assets.append(push_strategy_for_host['hint_resources'])
                        self.hint_mimetype.append(push_strategy_for_host['hint_mimetype'])

                        mimetypes = []
                        for i,mimetype in enumerate(push_strategy_for_host['hint_mimetype']):
                                if 'image' in mimetype:
                                        mimetypes.append('image')
                                elif 'css' in mimetype:
                                        mimetypes.append('style')
                                elif 'javascript' in mimetype:
                                        mimetypes.append('script')
                                elif 'font' in mimetype:
                                        mimetypes.append('font')
                                else: #xhr
                                        print "WARNING: unmatched mimetype",mimetype, push_strategy_for_host['hint_resources'][i]
                                        mimetypes.append('')

                        self.hint_as_string.append(mimetypes)

    def _query_replayserver(self, env, hostname, path):
        passed_env = dict()
        # remap for compat with replayserver
        passed_env['SERVER_PROTOCOL'] = "HTTP/1.1"
        passed_env['MAHIMAHI_CHDIR'] = env['WORKING_DIR']
        passed_env['MAHIMAHI_RECORD_PATH'] = env['RECORDING_DIR']
        passed_env['REQUEST_METHOD'] = 'GET' # we assume this is always get..
        passed_env['HTTPS'] = "1"
        passed_env['REQUEST_URI'] = path
        passed_env['HTTP_HOST'] = hostname

        if 'HTTP_RANGE' in env.keys():
            passed_env['HTTP_RANGE'] = env['HTTP_RANGE']

        p = subprocess.Popen([env['REPLAYSERVER_FN']], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=passed_env)
        (replay_stdout, _) = p.communicate()
        return replay_stdout

    def _init_push_responsecache(self):
        env = os.environ
        for i,host in enumerate(self.push_host):
            self.push_cache.append([])
            for j,asset_name in enumerate(self.push_assets[i]):
                parsed = urlparse(asset_name)
                stripped = asset_name[len('https://'+parsed.netloc):]
                asset_name = stripped
                passed_host = parsed.netloc

                # print "Caching... ", asset_name, parsed
                # load responses into cache
                self.push_cache[i].append(self._query_replayserver(env,passed_host,asset_name))


    def _init_custom_scheduler(self):
        #check if this instance of the replay app is running a custom scheduler
        env = os.environ
        if env['NGINX_CUSTOM_SCHEDULER'] == "1" and self.push_strategy_file != "noop":
            self.has_custom_strategy = True
            self.custom_strategy_host_trigger = env['NGINX_CUSTOM_TRIGGER_HOST']
            self.custom_strategy_path_trigger = env['NGINX_CUSTOM_TRIGGER_PATH']
            self.custom_strategy_offset_html = int(env['NGINX_CUSTOM_OFFSET'])

            custom_strategy_html_content_file = os.path.join(os.path.dirname(self.push_strategy_file), env['NGINX_CUSTOM_CONTENT_FILE'])

            print "Custom Replay Scheduler active for "+env['NGINX_CUSTOM_TRIGGER_HOST']

            replay_response = self._query_replayserver(env,self.custom_strategy_host_trigger,self.custom_strategy_path_trigger)
            self.custom_strategy_response = replay_response

            c = zlib.compressobj()

            with open(custom_strategy_html_content_file,'r') as fd:
                decompressed_index = fd.read()

                print decompressed_index[:self.custom_strategy_offset_html]

                #print decompressed_index
                self.custom_html_content_A = c.compress(decompressed_index[:self.custom_strategy_offset_html]) + c.flush(zlib.Z_SYNC_FLUSH)
                self.custom_html_content_B = c.compress(decompressed_index[self.custom_strategy_offset_html:]) + c.flush(zlib.Z_FINISH)

            self.CRITICAL_CSS_PATH = '/push__critical.css'
            critical_css_file = os.path.join(os.path.dirname(self.push_strategy_file), env['NGINX_CRITICAL_CSS_FILE'])
            print "Reading", critical_css_file
            c = zlib.compressobj()
            with open(critical_css_file,'r') as fd:
                self.CRITICAL_CSS_CONTENT = c.compress(fd.read()) + c.flush(zlib.Z_FINISH)
                print "Done Reading"


    def __init__(self):
        self.has_custom_strategy = False
        self.custom_strategy_host_trigger = ''
        self.custom_strategy_path_trigger = ''
        self.custom_html_content_A = ''
        self.custom_html_content_B = ''
        self.custom_strategy_offset_html = 0
        
        self.push_strategy_file = os.environ['PUSH_STRATEGY_FILE']
        self.push_host = []
        self.push_trigger_path = []
        self.push_assets = []
        self.push_cache = []

        self.hint_host = []
        self.hint_trigger_path = []
        self.hint_assets = []
        self.hint_as_string = []
        self.hint_mimetype = []

        self._parse_push_strategy()
        self._init_push_responsecache()
        self._init_custom_scheduler()

        


    def __call__(self, environ, start_response):
        #print "call! " + p['REQUEST_URI']
        #print environ
        hdrlist = []
        env = dict(environ)
        if env['HTTP_HOST'].endswith('.'):
           env['HTTP_HOST'] = env['HTTP_HOST'][:-1]
    

        #FastPath for Push Garbage:
        if env['REQUEST_METHOD'] == "GET" and env['REQUEST_URI'].startswith('/__GARBAGE__'):
            amount_kb = int(env['REQUEST_URI'].split('__')[2])
            N = amount_kb * 1024
            response = ''.join(random.choice(string.ascii_uppercase + string.digits) for _ in range(N))
            start_response('200 OK', [('Content-Type', 'text/plain')])
            yield response

        #FastPath for Push Critical CSS:
        elif env['REQUEST_METHOD'] == "GET" and env['HTTP_HOST'] == self.custom_strategy_host_trigger and env['REQUEST_URI'].startswith(self.CRITICAL_CSS_PATH):
            start_response("200 OK", 
            [
                ('Content-Type', 'text/css'),
                ('Content-Encoding', 'deflate'),
            ])
            yield self.CRITICAL_CSS_CONTENT

        else:

            cached_response = None
            is_push = False
            if env['REQUEST_METHOD'] == "GET":
                for u,host in enumerate(self.push_host):
                    if host == env['HTTP_HOST']:
                      for v,push_resource in enumerate(self.push_assets[u]):
                            parsed = urlparse(push_resource)
                            stripped = push_resource[len('https://'+parsed.netloc):]
                            push_resource = stripped
                            if push_resource == env['REQUEST_URI']:
                                #print "pushing from cache..."
                                is_push = True
                                cached_response = self.push_cache[u][v]
            
            #print env['HTTP_HOST'], env['REQUEST_URI']

            if cached_response is None:
                # print environ['REQUEST_URI'], "Not cached!"
                passed_env = dict()

                # remap for compat with replayserver
                passed_env['MAHIMAHI_CHDIR'] = env['WORKING_DIR']
                passed_env['MAHIMAHI_RECORD_PATH'] = env['RECORDING_DIR']
                passed_env['REQUEST_METHOD'] = env['REQUEST_METHOD']
                passed_env['REQUEST_URI'] = env['REQUEST_URI']

                # env['SERVER_PROTOCOL'], is currently a hack to find the corresponding
                # h1 traces
                passed_env['SERVER_PROTOCOL'] = "HTTP/1.1"
                passed_env['HTTP_HOST'] = env['HTTP_HOST']

                if 'HTTP_RANGE' in env.keys():
                    passed_env['HTTP_RANGE'] = env['HTTP_RANGE']

                if env['wsgi.url_scheme'] == 'https':
                    passed_env['HTTPS'] = "1"

                # shell=True,
                p = subprocess.Popen(
                    [env['REPLAYSERVER_FN']], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=passed_env)

                (cached_response, replay_stderr) = p.communicate()

            custom_interleaving = False
            # Check if we must deliver using a custom strategy:
            if env['REQUEST_METHOD'] == "GET" and env['HTTP_HOST'] == self.custom_strategy_host_trigger and env['REQUEST_URI'] == self.custom_strategy_path_trigger:
                custom_interleaving = True
                #print "Size: " + str(len(self.custom_html_content_A))

            if not custom_interleaving:
                response_header, response_body = cached_response.split('\r\n\r\n', 1)
            else:
                response_header, _ = self.custom_strategy_response.split('\r\n\r\n', 1)



            status_line, headers_alone = response_header.split('\r\n', 1)
            splitted_status = status_line.split(' ')

            # response_code = status_line[1]

            status_cleaned = ' '.join(splitted_status[1:])
            #print status_cleaned

            headers = Message(StringIO(headers_alone))

            #print env['REQUEST_METHOD'], env['REQUEST_URI']
           
            is_chunked = False
            corsfound = False
            hdrlist = []

            for key in headers.keys():
                if key == "transfer-encoding" and 'chunked' in headers[key]:
                    is_chunked = True
                else:
                    if custom_interleaving and key.lower() == 'content-encoding':
                        hdrlist.append((key.strip(), 'deflate'))
                    elif custom_interleaving and key.lower() == 'content-length':
                        continue
                    
                    else:
                        #Filter out any link header the site might emit.
                        if key not in ['expires', 'date', 'last-modified','link', 'alt-svc','content-security-policy']:
                            hdrlist.append((key.strip(), headers[key]))
                        if key.lower() == 'access-control-allow-origin':
                            corsfound = True
            
            
            if not corsfound: 
                # this is required for font hinting...
                # note that we will not overwrite cors headers b/c
                # in some xhr situations * is not sufficient
                hdrlist.append(('Access-Control-Allow-Origin','*'))

            
            if not is_push and env['SERVER_PROTOCOL'] == "HTTP/2":
                for i, push_host_strategy in enumerate(self.push_host):
                    if passed_env['HTTP_HOST'] == push_host_strategy:
                        if passed_env['REQUEST_URI'] == self.push_trigger_path[i]:
                            linkstr = ''
                            # TODO (bewo): is there any limitation?
                            for asset in self.push_assets[i]:
                                if linkstr != '':
                                     linkstr += ','
                                # print asset;
                                #print asset
                                linkstr += '<' + asset + '>; rel=preload'
                            hdrlist.append(('x-extrapush', str(linkstr)))
                            print 'WILL PUSH: ' ,len(self.push_assets[i]) #//, ('x-extrapush', str(linkstr))
                            break

            if not is_push:
                    for i, hint_host_strategy in enumerate(self.hint_host):
                        if passed_env['HTTP_HOST'] == hint_host_strategy:
                            if passed_env['REQUEST_URI'] == self.hint_trigger_path[i]:
                                linkstr = ''
                                for j, asset in enumerate(self.hint_assets[i]):
                                    if linkstr != '':
                                       linkstr += ','
                                    as_string = self.hint_as_string[i][j]
                                    if as_string != '':
                                       as_string='; as='+as_string+''
                                    if self.hint_as_string[i][j] == "font":
                                       as_string += ";crossorigin"
                                    linkstr_to_append = '<'+asset + '>; rel=preload'+as_string+';type="'+self.hint_mimetype[i][j]+'"'
                                    linkstr += linkstr_to_append
                                hdrlist.append(('link', str(linkstr)))
                                # print 'WILL HINT: ' ,len(self.hint_assets[i]) #//, ('x-extrapush', str(linkstr))
                                break

            # print "start response! " + environ['HTTP_HOST'] + " - " + environ['REQUEST_URI']

            if not custom_interleaving:
                if is_chunked:
                    # print "will decode chunked"
                    start_response(status_cleaned, hdrlist)
                    # print "chunked"
                    for chunk in decode(StringIO(response_body)):
                        yield str(chunk)
                else:
                    start_response(status_cleaned, hdrlist)
                    yield response_body
            else:
                #note that we already decode the chunk
                start_response(status_cleaned, hdrlist)
                yield self.custom_html_content_A
                yield self.custom_html_content_B


        #start_response('200 OK', [('Content-Type', 'text/html')])
        # yield replay_stdout
