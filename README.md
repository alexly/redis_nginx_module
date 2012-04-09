Description.
=============

This is an Nginx module that allows you to make requests to the Redis server in a non blocking mode. 
The module supports the Redis 2.x. It can operate with the protocols of TCP / Unix Domain Sockets, also supports pipelining.

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



Copyright & License
=============

This module is licenced under the ICE license.

Copyright (c) 2011-2012, Alexander Lyalin <alexandr.lyalin@gmail.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
