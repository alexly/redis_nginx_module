Overview.
=============

This is an Nginx module that allows you to make requests to the Redis server in a non blocking mode. 
The module supports the Redis 2.x. It can operate with the protocols of TCP / Unix Domain Sockets, also supports pipelining.

This module returns parsed response from the Redis server. It's recommended to use Redis server side LUA scripting. 
Thus, we can obtain the desired response format on the Redis Server side.

Module uses to parse json library  https://github.com/donhuanmatus/js0n/blob/master/README . 
It's one-pass json parser. Super low overhead to parsing json.

Response will be sent to the client only after the completion of an "asynchronous" operation. 
Redis Server processes requests in a single thread.
Operation is fully atomic and should not be performed more than 300 ms. 
For maximum performance scripts. And other operaion don't have to run for a long time Lua Scripts.


Lua Script example:
=============

    
    redis.call("set", "testkey", "testvalue");
    
    -- any processing ...
    
    local value = redis.call('get', 'testkey');
    
    json_value = { true, { foo = 'bar', val = value} }
    
    json_text = cjson.encode(value)
    
    return json_text;
    
    -- Returns: '[true,{'foo':'bar', '"val': 'testvalue'}]'
    


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

worker_processes  1;

daemon off;
master_process off;

events {
    worker_connections 2048;
}

http {

    server {
    
        listen        localhost;

        redis_master_node 127.0.0.1:6379;

        location /setbylua {
            add_header Content-Type "text/html; charset=UTF-8";
            redis_read_cmd_ret eval "redis.call('set', KEYS[1], KEYS[2]); return 'Success';" 2 $arg_key $arg_value;
        }

        location /getbylua {
            add_header Content-Type "text/html; charset=UTF-8";
            redis_read_cmd_ret eval "return redis.call('get',  KEYS[1])" 1 $arg_key;
        }

        location /get {
            add_header Content-Type "text/html; charset=UTF-8";
            redis_read_cmd_ret get $arg_key;
        }

        location /set {
            add_header Content-Type "text/html; charset=UTF-8";
            redis_write_cmd_ret set $arg_key $arg_value;
        }

        location /setstudents {
            add_header Content-Type "text/html; charset=UTF-8";
            # write to single master node
            # request body: "students: [ {StudentId:'', StudentName:''}]"
            # send to redis. And store with the key. 
            redis_write_cmd_ret eval "redis.call('set', 'students', KEYS[1]);" 1 @students;
            # massive of the students to the redis with key "students"
            #module supports the following compiled variables:
            #'$' nginx variable
            #'@' json field(from request body
            # default string constant
        }

        location /studentscount {
            # text/html are json or html 
            add_header Content-Type "text/html; charset=UTF-8";
            # read from single master node
            # returns students count
            redis_read_cmd_ret eval "local test = redis.call('get 'students');
                                    local parsed = cjson.parse(test);
                                    if(#parsed) then
                                        return #parsed;
                                    end
                                    
                                    return 0;" 0;
        }
                
        location / {
            root html;
            index index.html index.htm;
        }
    }
}

Copyright & License
=============

New BSD License http://dist.codehaus.org/janino/new_bsd_license.txt

Copyright (c) 2011-2012, Alexander Lyalin <alexandr.lyalin@gmail.com>
