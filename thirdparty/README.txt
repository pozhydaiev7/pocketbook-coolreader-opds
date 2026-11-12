PocketBook dependencies
======================

versions.json pins the bundled releases, upstream archive SHA-256 checksums
and license files. The five updated source directories contain complete,
unmodified upstream release trees. Keep upstream notices and licensing intact.
CHMLib 0.40 and Antiword 0.37 retain CoolReader's custom stream integration;
they are required for CHM and Word documents. TinyDict remains project code.

The maintained build always links private static FreeType 2.15.0,
libjpeg-turbo 3.2.0 (libjpeg API 62), libpng 1.6.59, zlib 1.3.2 and curl 8.23.0.
There is no automatic fallback to an older SDK image/compression/network
library. --exclude-libs,ALL prevents InkView from binding to those symbols.
InkView and the firmware's own transitive dependencies remain unchanged.

JPEG and curl use separate ExternalProject builds to avoid upstream CMake
integration restrictions and configure-check conflicts. They use the same
B288 toolchain. The build uses vendored sources without network downloads.
Application builds exclude upstream tools, tests, examples and optional
HarfBuzz/Brotli/HVF integrations. curl supports only HTTP and HTTPS, using
firmware OpenSSL and the updated private zlib. Unused curl integrations
(libssh2, libpsl, libidn2, nghttp2, c-ares, Brotli and Zstd) are disabled.
Disabled UnRAR 4.10 sources and the unused RAR engine path were removed;
RAR was not enabled in the previous Touch HD 3 builds. ZIP remains supported.
The obsolete bundled JPEG 6b, PNG 1.6.6 and zlib 1.2.3 trees were replaced.

HTTPS verifies both the peer certificate and hostname. The default trust
bundle is /ebrmain/share/ssl/certs/ca-certificates.crt from firmware. The
standard SSL_CERT_FILE environment variable can select a private CA bundle.
Requests and redirects are limited to HTTP(S), with bounded timeouts.

All bundled notices are included under licenses/ in candidate ZIPs, along
with this dependency description and versions.json. Build metadata records
the same dependency manifest. A physical-reader check is still required
before treating a candidate as a validated release.

This software is based in part on the work of the Independent JPEG Group.
Portions of this software are copyright 2026 The FreeType Project
(https://freetype.org). All rights reserved.

The CREngine migration additionally uses QImageScale from the same pinned
CoolReader source commit, without linking the Qt framework. GPLv2 is the chosen
Qt source-license alternative; source notices and the image-transform notices
are retained. This component is used by font/image scaling in the new engine.
