nginx http dynamic redirect module
==================================

This nginx module may be used to redirect some URIs dynamically based on configuration stored on redis.

_This module is not distributed with the Nginx source. See [the installation instructions](#installation)._


Configuration
-------------

The directives are allowed on location, server or main. Example on location:

    location /example {
        dynamic_redirect on;                                # default: off
        dynamic_redirect_redis_hostname redis.address.com;  # default: localhost
        dynamic_redirect_redis_port 1234;                   # default: 6379
        dynamic_redirect_redis_db 2;                        # default: 0
    }


<a id="installation"></a>Installation instructions
--------------------------------------------------

[Download Nginx Stable](http://nginx.org/en/download.html) source and extract it (ex.: to ../nginx). Then execute ./configure with --add-module pointing to this project as usual. Something like this:

    $ ./configure \
        --add-module=../ngx-http-dynamic-redirect-module \
        --prefix=/usr/local/nginx
    $ make
    $ make install



Redis data
----------

Set a key -> value data on redis to configure the redirect wanted.

    localhost:6379> set 'http://localhost:8080/example' 'http://github.com/dx7'


Expected result
---------------

    $ curl -i http://localhost:8080/example
    HTTP/1.1 301 Moved Permanently
    Server: nginx/1.9.14
    Date: Mon, 15 Aug 2016 22:30:32 GMT
    Content-Length: 0
    Connection: keep-alive
    Location: http://github.com/dx7
