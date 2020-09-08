# stdarc.c
Small file archivers (zip, pak, tar), virtual filesystem (vfs) and dir listing in a single-file header.

# Features
- ZIP archive reading/writing/appending. (`*`)
- PAK archive reading/writing/appending.
- TAR archive reading.
- VFS mounting/loading.
- DIR listing.

# Project goals
- C.
- Drop & use.
- Small codebase.
- Good enough performance.
- Public domain or unlicensed source code. No attribution required.

# Compressors (deflate, lzma, lz4, ...)
- `*`: Compressors (not only deflate!) are supplied apart. See https://github.com/r-lyeh/stdpack.c instead

# Todo
- Bugfixes and cleaning up.

# Links
- Joonas Pihlajamaa: junzip - https://github.com/jokkebk/JUnzip/
