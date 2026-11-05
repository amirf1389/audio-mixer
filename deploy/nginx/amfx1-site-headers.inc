# security headers for the static website pages (keys-api.conf): copy to /etc/nginx/amfx1-site-headers.inc
# (an add_header inside a location replaces the server's ones, so every site location includes this file)
add_header Content-Security-Policy $site_csp always;
add_header Strict-Transport-Security "max-age=63072000; includeSubDomains" always;
add_header X-Content-Type-Options "nosniff" always;
add_header X-Frame-Options "DENY" always;
add_header Referrer-Policy "strict-origin-when-cross-origin" always;
add_header Permissions-Policy "camera=(), microphone=(), geolocation=(), usb=(), bluetooth=(), interest-cohort=()" always;
add_header Cross-Origin-Opener-Policy "same-origin" always;
add_header Cross-Origin-Resource-Policy "same-origin" always;
