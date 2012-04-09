
Description.
=============

This is an Nginx module that allows you to make requests to the Redis server in a non blocking mode. 
The module supports the Redis 2.x. It can operate with the protocols of TCP / Unix Domain Sockets, also supports pipeliningю

This module returns parsed response from the Redis server. It's recommended to use Redis server side scripting(LUA). 
Thus, we can obtain the desired response format on the Redis Server side.

Example:

redis.call("set", "testkey", "testvalue");
...
value= redis.call("get", “testkey");
json_value = { true, { foo = "bar", val= value} }
json_text = cjson.encode(value)
return json_text;

-- Returns: '[true,{"foo":"bar", "val": "testvalue"}]'



ICE License
=============

Copyright (c) 2011-2012, Alexander Lyalin <alexandr.lyalin@gmail.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.
