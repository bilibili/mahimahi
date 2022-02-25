#!/usr/bin/env python
# -*- coding: UTF-8 -*-

from cgi import escape
import os
from flup.server.fcgi import WSGIServer
#from flup.server.fcgi_single import WSGIServer
from ReplayApp import ReplayApp

app = ReplayApp()

#WSGIServer(app,multiplexed=False,multithreaded=False,minSpare=2, maxSpare=12).run()


# note to myself: this setting is fucking important for interleaved push.
# any sort of mulitithreading/high concurrency will FUCK UP the push order!
# Never ever use multithreaded WSGI otherwise custom scheduler wont work
WSGIServer(app,multiplexed=True,multithreaded=False).run()
