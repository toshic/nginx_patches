
Table of Contents
=================

* [Status](#status)
* [Compatibility](#compatibility)
* [Installation](#installation)
* [Eblob module](#eblob-module)
* [FLV filter module](#flv-filter-module)


Status
======

These modules are production ready.

[Back to TOC](#table-of-contents)


Compatibility
=============

The following versions of Nginx should work with these modules:

* 1.5.x
* 1.4.x
* 1.3.x
* 1.2.x

[Back to TOC](#table-of-contents)


Installation
============

* Grab the nginx source code from [nginx.org](http://nginx.org), for example, the version 1.5.8 (see [Nginx Compatibility](#compatibility)),
* after that, download the latest version of this module
* and finally build the Nginx source with this module
```nginx

    wget 'http://nginx.org/download/nginx-1.5.8.tar.gz'
    tar -xzvf nginx-1.5.8.tar.gz
    cd nginx-1.5.8/
 
    # Here we assume you would install you nginx under /opt/nginx/.
    ./configure --prefix=/opt/nginx \
         --add-module=/path/to/nginx-eblob

    make
    make install
```

[Back to TOC](#table-of-contents)


Eblob module
============

This module was created for cooperation with Elliptics storage. It allows direct downloads from storage servers. You may want to use this module if you want to decrease network usage on your Elliptics' proxy servers.  In order to prevent unauthorized downloads there is a digital signature of request url. (Will be implemented in this module future releases).

Directives
----------

**eblob**
**syntax:** mp4
**default:** n/a
**context:** location
Enable streaming from eblob at a particular location

[Back to TOC](#table-of-contents)

FLV filter module
=================

Basically it's a FLV pseudostreaming module reimplemented as a filter. Thus it is able to work in cooperation with nginx's cache or eblob module.

Directives
----------

**flv_filter**
**syntax:** flv_filter (off|cached|on)
**default:** off
**context:** location
on - Enable FLV pseudostreaming filter for all requests
cached - Enable FLV pseudostreaming filter only for requests that were already cached
off - Disable FLV pseudostreaming filter

[Back to TOC](#table-of-contents)
