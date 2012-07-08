Overview.
=============

This is an Nginx module that allows you to make requests to the Redis server in a non blocking mode. 
The module supports the Redis 2.x. It can operate with the protocols of TCP / Unix Domain Sockets, also supports pipelining.

This module returns parsed response from the Redis server. It's recommended to use Redis server side scripting(LUA). 
Thus, we can obtain the desired response format on the Redis Server side.
The module is intended for the issuance of outside lines json, xml, or error code. I like json. 


You can on the side of the server Redis to do the following operations with LUA scripts. 

Operation is fully atomistic and should not be performed more than a second.

Lua Script example:
=============
redis.call("set", "testkey", "testvalue");

...

value= redis.call("get", “testkey");

json_value = { true, { foo = "bar", val= value} }

json_text = cjson.encode(value)

return json_text;

-- Returns: '[true,{"foo":"bar", "val": "testvalue"}]'

How to build this nginx module:
=============

Tested on Ubuntu 11.04/12.04 LTS. 
Download [http://nginx.org/ nginx]  source. Put it ro folder.

chechout redis4nginx https://github.com/donhuanmatus/redis4nginx.git to nginx/redis4nginx

make clean
./configure --add-module=./redis4nginx
make
or 
sudo make and install

to run:
./nginx


Simple nginx config:
=============

http {
charset utf-8;
    include       mime.types;
    default_type  application/octet-stream;

    sendfile        on;

    server {
        listen       80;
        server_name  localhost;

        # previous idea to use the global(single) lua state enviroment.
        # todo: associate lua script file with the name of a function
        redis_common_script conf/common_script.lua;
        redis_master_node 127.0.0.1:6379;

        location /set {
            add_header Content-Type "text/html; charset=UTF-8";
            # write to single master node
            # students: [ {StudentId:"", StudentName:""}, .. {..} ]
            # send to redis. And store with the key. 
            redis_read_cmd_ret set test @students;
        }

        location /count {
            # text/html are json or html 
            add_header Content-Type "text/html; charset=UTF-8";
            # read from single master node
            # returns students count
            redis_exec_return eval "return #(cjson.parse(redis.call('get "test'))) 0;
        }

        location / {
            root   html;
            index  index.html index.htm;
        }
    }

}

Copyright & License
=============

New BSD License

Copyright (c) 2011-2012, Alexander Lyalin <alexandr.lyalin@gmail.com>
