# AmF X1 website

Static, script-free pages for `https://amirfakhri.interactivelink.site` (home, features, download, developers, security, FAQ, privacy, 404),
plus the hand-written key request page `license.html` (served at `/keys`).

## Build
    node scripts/build-website.js            # rewrites the .html pages, sitemap.xml and robots.txt from one source
    node scripts/build-website.js --site https://example.com   # another domain

Edit the page text in `scripts/build-website.js`, not in the generated `.html` files. The version shown comes from `package.json`
(and `releaseChannel`, e.g. "1.22.1.0 Beta").

## Deploy (nginx, HTTPS)
    sudo mkdir -p /srv/amfx1-website && sudo cp -r deploy/website/* /srv/amfx1-website/
    sudo cp deploy/nginx/keys-api.conf /etc/nginx/conf.d/
    sudo cp deploy/nginx/keys-api-proxy.inc deploy/nginx/amfx1-site-headers.inc /etc/nginx/
    sudo nginx -t && sudo nginx -s reload

Clean URLs (`/features`), one canonical URL per page (`/features.html` redirects), a strict Content-Security-Policy
(`default-src 'none'; style-src 'self'; img-src 'self'`), HSTS, and a year of caching for `/assets/` (URLs carry `?v=<version>`).

## CDN
Proxy the domain through any CDN with "Full (strict)" TLS to the origin. Cache `/assets/*` (the origin already sends
`immutable`), revalidate HTML (`no-cache`), and never cache `/keys`, `/request-license`, `/update.json`, `/vendor.json` or `/releases/`.
No third-party scripts, fonts or CDN libraries are loaded, so there is nothing external to pin with Subresource Integrity.

## SEO
Every page has a unique title and description, a canonical URL, Open Graph / Twitter tags and JSON-LD (WebPage, BreadcrumbList,
SoftwareApplication, Organization, FAQPage, TechArticle). Submit `https://amirfakhri.interactivelink.site/sitemap.xml` in Google Search Console
and Bing Webmaster Tools after the first deploy.
