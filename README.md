Overview.
=============

This is an Nginx module that allows you to make requests to the Redis server in a non blocking mode. 
The module supports the Redis 2.x. It can operate with the protocols of TCP / Unix Domain Sockets, also supports pipelining.

This module returns parsed response from the Redis server. It's recommended to use Redis server side scripting(LUA). 
Thus, we can obtain the desired response format on the Redis Server side.
The module is intended for the issuance of outside lines json, xml, or error code. I like json. 
He uses to parse json this library [https://github.com/donhuanmatus/js0n/blob/master/README js0n]. 
It's one-pass super low overhead to parsing json.
You can on the side of the server Redis to do the following operations with LUA scripts.

Response will be sent to the client only after the completion of an "asynchronous" operation. 
Redis Server processes requests in a single thread.
Operation is fully atomistic and should not be performed more than 100-500 ms. 
For maximum performance scripts and other operaion do not have to run for a long time and handle large volumes of data

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
                    redis_read_cmd_ret eval "set test KEYS[1]" 1 @students;
                }
        
                location /count {
                    # text/html are json or html 
                    add_header Content-Type "text/html; charset=UTF-8";
                    # read from single master node
                    # returns students count
                    redis_read_cmd_ret eval "local test = redis.call('get 'test');
                                            local parsed = cjson.parse(test);
                                            if(#parsed) then
                                                return #parsed;
                                            end
                                            
                                            return 0;" 0;
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
