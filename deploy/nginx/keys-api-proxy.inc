# shared proxy settings for keys-api.conf: copy to /etc/nginx/keys-api-proxy.inc
proxy_http_version 1.1;
proxy_set_header Connection "";
proxy_set_header Host 127.0.0.1;                 # the generator's public listener does not need the public Host
proxy_set_header X-Real-IP $remote_addr;         # used for its per-sender limits (trusted only from loopback)
proxy_set_header X-Forwarded-For "";             # never pass a client-supplied chain
proxy_set_header Authorization "";
proxy_set_header Cookie "";
proxy_connect_timeout 3s;
proxy_read_timeout 15s;
proxy_hide_header X-Powered-By;
