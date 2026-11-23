{
	auto_https off
}
http://example.com {
	reverse_proxy 127.0.0.1:8080
}
