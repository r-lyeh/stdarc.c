// stdarc.c
// - rlyeh, public domain

#ifndef STDARC_H
#define STDARC_H
#define STDARC_VERSION "v1.0.0"
#endif

#ifdef STDARC_C
#pragma once
#define ZIP_C
#define TAR_C
#define PAK_C
#define VFS_C
#define DIR_C
#endif

#line 1 "src/zip.c" 
// zip un/packer. based on JUnzip library by Joonas Pihlajamaa (UNLICENSE)
// - rlyeh, public domain.
//
// notes about compression_level:
// - plain integers use DEFLATE. Levels are [0(store)..6(default)..9(max)] 
// - stdarc compressor flags are also supported. Just use LZMA|5, ULZ|9, LZ4X|3, etc.
// - see zip_put.c for more info.
//
//@todo: +w) int zip_append(zip*, const char *entryname, const void *buf, unsigned buflen);
//@todo: +w) int zip_append_mem(zip*, const char *entryname, const void *buf, unsigned buflen, unsigned compr_level);

#ifndef ZIP_H
#define ZIP_H
#include <stdio.h>
#include <stdbool.h>

typedef struct zip zip;

zip* zip_open(const char *file, const char *mode /*r,w,a*/);

    // only for (w)rite or (a)ppend mode
    bool zip_append_file(zip*, const char *entryname, FILE *in, unsigned int compr_level);

    // only for (r)ead mode
    unsigned int zip_find(zip*, const char *entryname); // convert entry to index. returns <0 if not found.
    unsigned int zip_count(zip*);
        char*        zip_name(zip*, unsigned int index);
        char*        zip_modt(zip*, unsigned int index);
        unsigned int zip_size(zip*, unsigned int index);
        unsigned int zip_hash(zip*, unsigned int index);
        bool         zip_file(zip*, unsigned int index); // is_file? (dir if name ends with '/'; file otherwise)
        bool         zip_test(zip*, unsigned int index);
        unsigned int zip_codec(zip*, unsigned int index);
        unsigned int zip_offset(zip*, unsigned int index);
        void*        zip_extract(zip*, unsigned int index); // must free() after use
        bool         zip_extract_file(zip*, unsigned int index, FILE *out);
        unsigned int zip_extract_data(zip*, unsigned int index, void *out, unsigned int outlen);

void zip_close(zip*);

#endif // ZIP_H

// -----------------------------------------------------------------------------

#ifdef ZIP_C
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef REALLOC
#define REALLOC realloc
#endif

#ifndef STRDUP
#define STRDUP  strdup
#endif

#ifndef FPRINTF
#define FPRINTF(...)    ((void)0) // printf for error logging
#endif

#ifndef PRINTF
#define PRINTF(...)     ((void)0) // printf for debugging
#endif

#ifndef ERR
#define ERR(NUM, ...)   (FPRINTF(stderr, "" __VA_ARGS__), FPRINTF(stderr, "(%s:%d)\n", __FILE__, __LINE__), /*fflush(stderr),*/ (NUM)) // (NUM)
#endif

#ifndef COMPRESS
#define COMPRESS(...)   ((unsigned)0)
#endif

#ifndef DECOMPRESS
#define DECOMPRESS(...) ((unsigned)0)
#endif

#ifndef BOUNDS
#define BOUNDS(...)     ((unsigned)0)
#endif

#ifdef STDPACK_H
    #undef COMPRESS
    #undef DECOMPRESS
    #undef BOUNDS
    static unsigned COMPRESS(const void *in, unsigned inlen, void *out, unsigned outlen, unsigned flags /*[0..1]*/) {
        return ( flags > 10 ? mem_encode : deflate_encode )(in, inlen, out, outlen, flags);
    }
    static unsigned DECOMPRESS(const void *in, unsigned inlen, void *out, unsigned outlen, unsigned flags) {
        return ( flags ? mem_decode : deflate_decode )(in, inlen, out, outlen);
    }
    static unsigned BOUNDS(unsigned inlen, unsigned flags) {
        return ( flags > 10 ? mem_bounds : deflate_bounds )(inlen, flags);
    }
#elif defined DEFLATE_C
    #undef COMPRESS
    #undef DECOMPRESS
    #undef BOUNDS
    static unsigned COMPRESS(const void *in, unsigned inlen, void *out, unsigned outlen, unsigned flags /*[0..1]*/) {
        return deflate_encode(in, inlen, out, outlen, flags);
    }
    static unsigned DECOMPRESS(const void *in, unsigned inlen, void *out, unsigned outlen, unsigned flags) {
        return deflate_decode(in, inlen, out, outlen);
    }
    static unsigned BOUNDS(unsigned inlen, unsigned flags) {
        return deflate_bounds(inlen, flags);
    }
#endif

#pragma pack(push, 1)

typedef struct {
    uint32_t signature; // 0x02014B50
    uint16_t versionMadeBy; // unsupported
    uint16_t versionNeededToExtract; // unsupported
    uint16_t generalPurposeBitFlag; // unsupported
    uint16_t compressionMethod; // 0-store,8-deflate
    uint16_t lastModFileTime;
    uint16_t lastModFileDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLength;
    uint16_t extraFieldLength; // unsupported
    uint16_t fileCommentLength; // unsupported
    uint16_t diskNumberStart; // unsupported
    uint16_t internalFileAttributes; // unsupported
    uint32_t externalFileAttributes; // unsupported
    uint32_t relativeOffsetOflocalHeader;
} JZGlobalFileHeader;

typedef struct {
    uint32_t signature; // 0x06054b50
    uint16_t diskNumber; // unsupported
    uint16_t centralDirectoryDiskNumber; // unsupported
    uint16_t numEntriesThisDisk; // unsupported
    uint16_t numEntries;
    uint32_t centralDirectorySize;
    uint32_t centralDirectoryOffset;
    uint16_t zipCommentLength;
    // Followed by .ZIP file comment (variable size)
} JZEndRecord;

#pragma pack(pop)

// Verifying that structs are correct sized...
typedef int static_assert_sizeof_JZGlobalFileHeader[sizeof(JZGlobalFileHeader) == 46];
typedef int static_assert_sizeof_JZEndRecord[sizeof(JZEndRecord) == 22];
enum { sizeof_JZLocalFileHeader = 30 };

// Constants
enum { JZ_OK = 0, JZ_ERRNO = -1, JZ_BUFFER_SIZE = 65536 };

// Callback prototype for central reading function. Returns Z_OK while done.
typedef int (*JZRecordCallback)(FILE *fp, int index, JZGlobalFileHeader *header, 
    char *filename, void *extra, char *comment, void *user_data);

// Read ZIP file end record. Will move within file. Returns Z_OK, or error code
int jzReadEndRecord(FILE *fp, JZEndRecord *endRecord) {
    long fileSize, readBytes, i;
    JZEndRecord *er;

    if(fseek(fp, 0, SEEK_END)) {
        return ERR(JZ_ERRNO, "Couldn't go to end of zip file!");
    }

    if((fileSize = ftell(fp)) <= sizeof(JZEndRecord)) {
        return ERR(JZ_ERRNO, "Too small file to be a zip!");
    }

    unsigned char jzBuffer[JZ_BUFFER_SIZE]; // maximum zip descriptor size

    readBytes = (fileSize < sizeof(jzBuffer)) ? fileSize : sizeof(jzBuffer);

    if(fseek(fp, fileSize - readBytes, SEEK_SET)) {
        return ERR(JZ_ERRNO, "Cannot seek in zip file!");
    }

    if(fread(jzBuffer, 1, readBytes, fp) < readBytes) {
        return ERR(JZ_ERRNO, "Couldn't read end of zip file!");
    }

    // Naively assume signature can only be found in one place...
    for( i = readBytes - sizeof(JZEndRecord); i >= 0; i-- ) {
        er = (JZEndRecord *)(jzBuffer + i);
        if(er->signature == 0x06054B50)
            break;
    }

    if(i < 0) {
        return ERR(JZ_ERRNO, "End record signature not found in zip!");
    }

    memcpy(endRecord, er, sizeof(JZEndRecord));

    JZEndRecord *e = endRecord;
    PRINTF("end)\n\tsignature: 0x%X\n", e->signature ); // 0x06054b50
    PRINTF("\tdiskNumber: %d\n", e->diskNumber ); // unsupported
    PRINTF("\tcentralDirectoryDiskNumber: %d\n", e->centralDirectoryDiskNumber ); // unsupported
    PRINTF("\tnumEntriesThisDisk: %d\n", e->numEntriesThisDisk ); // unsupported
    PRINTF("\tnumEntries: %d\n", e->numEntries );
    PRINTF("\tcentralDirectorySize: %u %#x\n", e->centralDirectorySize, e->centralDirectorySize );
    PRINTF("\tcentralDirectoryOffset: %u %#x\n", e->centralDirectoryOffset, e->centralDirectoryOffset );
    PRINTF("\tzipCommentLength: %d\n---\n", e->zipCommentLength );

    if(endRecord->diskNumber || endRecord->centralDirectoryDiskNumber ||
            endRecord->numEntries != endRecord->numEntriesThisDisk) {
        return ERR(JZ_ERRNO, "Multifile zips not supported!");
    }

    return JZ_OK;
}

// Read ZIP file global directory. Will move within file. Returns Z_OK, or error code
// Callback is called for each record, until callback returns zero
int jzReadCentralDirectory(FILE *fp, JZEndRecord *endRecord, JZRecordCallback callback, void *user_data) {
    JZGlobalFileHeader fileHeader;

    if(fseek(fp, endRecord->centralDirectoryOffset, SEEK_SET)) {
        return ERR(JZ_ERRNO, "Cannot seek in zip file!");
    }

    {
        uint16_t i;
        for (i = 0; i < endRecord->numEntries; i++) {
            PRINTF("%d)\n@-> %lu %#lx\n", i + 1, (unsigned long) ftell(fp), (unsigned long) ftell(fp));
            long offset = ftell(fp); // store current position

            if (fread(&fileHeader, 1, sizeof(JZGlobalFileHeader), fp) < sizeof(JZGlobalFileHeader)) {
                return ERR(JZ_ERRNO, "Couldn't read file header #%d!", i);
            }

            JZGlobalFileHeader *g = &fileHeader, copy = *g;
            PRINTF("\tsignature: %u %#x\n", g->signature, g->signature); // 0x02014B50
            PRINTF("\tversionMadeBy: %u %#x\n", g->versionMadeBy, g->versionMadeBy); // unsupported
            PRINTF("\tversionNeededToExtract: %u %#x\n", g->versionNeededToExtract,
                   g->versionNeededToExtract); // unsupported
            PRINTF("\tgeneralPurposeBitFlag: %u %#x\n", g->generalPurposeBitFlag,
                   g->generalPurposeBitFlag); // unsupported
            PRINTF("\tcompressionMethod: %u %#x\n", g->compressionMethod, g->compressionMethod); // 0-store,8-deflate
            PRINTF("\tlastModFileTime: %u %#x\n", g->lastModFileTime, g->lastModFileTime);
            PRINTF("\tlastModFileDate: %u %#x\n", g->lastModFileDate, g->lastModFileDate);
            PRINTF("\tcrc32: %#x\n", g->crc32);
            PRINTF("\tcompressedSize: %u\n", g->compressedSize);
            PRINTF("\tuncompressedSize: %u\n", g->uncompressedSize);
            PRINTF("\tfileNameLength: %u\n", g->fileNameLength);
            PRINTF("\textraFieldLength: %u\n", g->extraFieldLength); // unsupported
            PRINTF("\tfileCommentLength: %u\n", g->fileCommentLength); // unsupported
            PRINTF("\tdiskNumberStart: %u\n", g->diskNumberStart); // unsupported
            PRINTF("\tinternalFileAttributes: %#x\n", g->internalFileAttributes); // unsupported
            PRINTF("\texternalFileAttributes: %#x\n", g->externalFileAttributes); // unsupported
            PRINTF("\trelativeOffsetOflocalHeader: %u %#x\n", g->relativeOffsetOflocalHeader,
                   g->relativeOffsetOflocalHeader);

            if (fileHeader.signature != 0x02014B50) {
                return ERR(JZ_ERRNO, "Invalid file header signature %#x #%d!", fileHeader.signature, i);
            }

            if (fileHeader.fileNameLength + 1 >= JZ_BUFFER_SIZE) {
                return ERR(JZ_ERRNO, "Too long file name %u #%d!", fileHeader.fileNameLength, i);
            }

            // filename
            char jzFilename[JZ_BUFFER_SIZE / 3];
            if (fread(jzFilename, 1, fileHeader.fileNameLength, fp) < fileHeader.fileNameLength) {
                return ERR(JZ_ERRNO, "Couldn't read filename #%d!", i);
            }
            jzFilename[fileHeader.fileNameLength] = '\0'; // NULL terminate

            // extra block
            unsigned char jzExtra[JZ_BUFFER_SIZE / 3];
            if (fread(jzExtra, 1, fileHeader.extraFieldLength, fp) < fileHeader.extraFieldLength) {
                return ERR(JZ_ERRNO, "Couldn't read extra block #%d!", i);
            }

            // comment block
            char jzComment[JZ_BUFFER_SIZE / 3];
            if (fread(jzComment, 1, fileHeader.fileCommentLength, fp) < fileHeader.fileCommentLength) {
                return ERR(JZ_ERRNO, "Couldn't read comment block #%d!", i);
            }
            jzComment[fileHeader.fileCommentLength] = '\0'; // NULL terminate

            // seek to local file header, then skip file header + filename + extra field length
            if (fseek(fp, fileHeader.relativeOffsetOflocalHeader + sizeof_JZLocalFileHeader - 2 - 2, SEEK_SET)) {
                return ERR(JZ_ERRNO, "Cannot seek in file!");
            }

            if (fread(&fileHeader.fileNameLength, 1, 2, fp) < 2) {
                return ERR(JZ_ERRNO, "Couldn't read local filename #%d!", i);
            }
            if (fread(&fileHeader.extraFieldLength, 1, 2, fp) < 2) {
                return ERR(JZ_ERRNO, "Couldn't read local extrafield #%d!", i);
            }
            if (fseek(fp,
                      fileHeader.relativeOffsetOflocalHeader + sizeof_JZLocalFileHeader + fileHeader.fileNameLength +
                      fileHeader.extraFieldLength, SEEK_SET)) {
                return ERR(JZ_ERRNO, "Cannot seek in file!");
            }

            PRINTF("@-> %lu %#lx\n---\n", (unsigned long) ftell(fp), (unsigned long) ftell(fp));

            if (JZ_OK != callback(fp, i, &fileHeader, jzFilename, jzExtra, jzComment, user_data))
                break; // keep going while callback returns ok

            fseek(fp, offset, SEEK_SET); // return to position
            fseek(fp, sizeof(JZGlobalFileHeader) + copy.fileNameLength, SEEK_CUR); // skip entry
            fseek(fp, copy.extraFieldLength + copy.fileCommentLength, SEEK_CUR); // skip entry
        }
    }

    return JZ_OK;
}

// Read data from file stream, described by header, to preallocated buffer. Returns Z_OK, or error code
int jzReadData(FILE *fp, JZGlobalFileHeader *header, void *out) {
    if(header->compressionMethod == 0) { // Store - just read it
        if(fread(out, 1, header->uncompressedSize, fp) < header->uncompressedSize || ferror(fp))
            return JZ_ERRNO;
    } else if((header->compressionMethod & 255) == 8) { // Deflate
        uint16_t level = header->compressionMethod >> 8;
        unsigned outlen = header->uncompressedSize;
        unsigned inlen = header->compressedSize;
        void *in = REALLOC(0, inlen);
        if(in == NULL) return ERR(JZ_ERRNO, "Could not allocate mem for decompress");
        unsigned read = fread(in, 1, inlen, fp);
        if(read != inlen) return ERR(JZ_ERRNO, "Could not read file"); // TODO: more robust read loop
        unsigned ret = DECOMPRESS(in, inlen, out, outlen, level);
        REALLOC(in, 0);
        if(!ret) return ERR(JZ_ERRNO, "Could not decompress");
    } else {
        return JZ_ERRNO;
    }

    return JZ_OK;
}

#define JZHOUR(t) ((t)>>11)
#define JZMINUTE(t) (((t)>>5) & 63)
#define JZSECOND(t) (((t) & 31) * 2)
#define JZTIME(h,m,s) (((h)<<11) + ((m)<<5) + (s)/2)

#define JZYEAR(t) (((t)>>9) + 1980)
#define JZMONTH(t) (((t)>>5) & 15)
#define JZDAY(t) ((t) & 31)
#define JZDATE(y,m,d) ((((y)-1980)<<9) + ((m)<<5) + (d))

// end of junzip.c ---

struct zip {
    FILE *in, *out;
    struct zip_entry {
    JZGlobalFileHeader header;
    char timestamp[20];
    char *filename;
    uint64_t offset;
    void *extra;
    char *comment;
    } *entries;
    unsigned count;
};

uint32_t zip__crc32(uint32_t crc, const void *data, size_t n_bytes) {
    // CRC32 routine is from Björn Samuelsson's public domain implementation at http://home.thep.lu.se/~bjorn/crc/ 
    static uint32_t table[256] = {0};
    if(!*table) {
        uint32_t i;
        for (i = 0; i < 0x100; ++i) {
            uint32_t r = i;
            unsigned short j;
            for (j = 0; j < 8; ++j) r = (r & 1 ? 0 : (uint32_t) 0xEDB88320L) ^ r >> 1;
            table[i] = r ^ (uint32_t) 0xFF000000L;
        }
    }
    {
        size_t i;
        for (i = 0; i < n_bytes; ++i) {
            crc = table[(uint8_t) crc ^ ((uint8_t *) data)[i]] ^ crc >> 8;
        }
    }
    return crc;
}

int zip__callback(FILE *fp, int idx, JZGlobalFileHeader *header, char *filename, void *extra, char *comment, void *user_data) {
    zip *z = user_data;
    unsigned index = z->count;
    z->entries = REALLOC(z->entries, (++z->count) * sizeof(struct zip_entry) );

    struct zip_entry *e = &z->entries[index];
    e->header = *header;
    e->filename = STRDUP(filename);
    e->offset = ftell(fp);
    e->extra = REALLOC(0, header->extraFieldLength);
    memcpy(e->extra, extra, header->extraFieldLength);
    e->comment = STRDUP(comment);

    snprintf(e->timestamp, sizeof(e->timestamp), "%04d/%02d/%02d %02d:%02d:%02d",
        JZYEAR(header->lastModFileDate), JZMONTH(header->lastModFileDate), JZDAY(header->lastModFileDate),
        JZHOUR(header->lastModFileTime), JZMINUTE(header->lastModFileTime), JZSECOND(header->lastModFileTime));

    return JZ_OK;
}

// zip read

unsigned int zip_find(zip *z, const char *entryname) {
    if( z->in ) {
        unsigned i;
        for ( i = z->count; --i >= 0; ) { // in case of several copies, grab most recent file (last coincidence)
            if (0 == strcmp(entryname, z->entries[i].filename)) return i;
        }
    }
    return -1;
}

bool zip_file(zip *z, unsigned index) { // is_file? (dir if attrib&15 or name ends with '/'; file otherwise)
    if( z->in && index < z->count ) {
        char *name = zip_name(z, index);
        return (name[ strlen(name) ] != '/') && !(z->entries[index].header.externalFileAttributes & 0x10);
    }
    return 0;
}

unsigned zip_count(zip *z) {
    return z->in ? z->count : 0;
}

unsigned zip_hash(zip *z, unsigned index) {
    return z->in && index < z->count ? z->entries[index].header.crc32 : 0;
}

char *zip_modt(zip *z, unsigned index) {
    return z->in && index < z->count ? z->entries[index].timestamp : 0;
}

char *zip_name(zip *z, unsigned index) {
    return z->in && index < z->count ? z->entries[index].filename : NULL;
}

unsigned zip_size(zip *z, unsigned index) {
    return z->in && index < z->count ? z->entries[index].header.uncompressedSize : 0;
}

unsigned zip_offset(zip *z, unsigned index) {
    return z->in && index < z->count ? z->entries[index].offset : 0;
}

unsigned zip_codec(zip *z, unsigned index) {
    if( z->in && index < z->count ) {
        unsigned cm = z->entries[index].header.compressionMethod;
        return cm < 255 ? cm : cm >> 8;
    }
    return 0;
}

unsigned zip_extract_data(zip* z, unsigned index, void *out, unsigned outlen) {
    if( z->in && index < z->count ) {
        JZGlobalFileHeader *header = &(z->entries[index].header);
        if( outlen <= header->uncompressedSize ) {
            fseek(z->in, z->entries[index].offset, SEEK_SET);
            int ret = jzReadData(z->in, header, (char*)out);
            return ret == JZ_OK ? header->uncompressedSize : 0;
        }
    }
    return 0;
}

void *zip_extract(zip *z, unsigned index) { // must free()
    if( z->in && index < z->count ) {
        unsigned outlen = (unsigned)z->entries[index].header.uncompressedSize;
        void *out = (char*)REALLOC(0, outlen);
        unsigned ret = zip_extract_data(z, index, out, outlen);
        return ret ? out : (REALLOC(out, 0), out = 0);
    }
    return NULL;
}

bool zip_extract_file(zip* z, unsigned index, FILE *out) {
    void *data = zip_extract(z, index);
    if( !data ) return false;
    unsigned datalen = (unsigned)z->entries[index].header.uncompressedSize;
    bool ok = fwrite(data, 1, datalen, out) == datalen;
    REALLOC( data, 0 );
    return ok;
}

bool zip_test(zip *z, unsigned index) {
    void *ret = zip_extract(z, index);
    bool ok = !!ret;
    REALLOC(ret, 0);
    return ok;
}

// zip append/write

bool zip_append_file(zip *z, const char *entryname, FILE *in, unsigned compress_level) {
    if( !in ) return ERR(false, "No input file provided");
    if( !entryname ) return ERR(false, "No filename provided");

    struct stat st;
    struct tm *timeinfo;
    stat(entryname, &st);
    timeinfo = localtime(&st.st_mtime);

    uint32_t crc = 0;
    unsigned char buf[1<<15];
    while(!feof(in) && !ferror(in)) crc = zip__crc32(crc, buf, fread(buf, 1, sizeof(buf), in));
    if(ferror(in)) return ERR(false, "Error while calculating CRC, skipping store.");

    unsigned index = z->count;
    z->entries = REALLOC(z->entries, (++z->count) * sizeof(struct zip_entry));
    if(z->entries == NULL) return ERR(false, "Failed to allocate new entry!");

    struct zip_entry *e = &z->entries[index], zero = {0};
    *e = zero;
    e->filename = STRDUP(entryname);

    e->header.signature = 0x02014B50;
    e->header.versionMadeBy = 10; // random stuff
    e->header.versionNeededToExtract = 10;
    e->header.generalPurposeBitFlag = 0;
    e->header.lastModFileTime = JZTIME(timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    e->header.lastModFileDate = JZDATE(timeinfo->tm_year+1900,timeinfo->tm_mon+1,timeinfo->tm_mday);
    e->header.crc32 = crc;
    e->header.uncompressedSize = ftell(in);
    e->header.fileNameLength = strlen(entryname);
    e->header.extraFieldLength = 0;
    e->header.fileCommentLength = 0;
    e->header.diskNumberStart = 0;
    e->header.internalFileAttributes = 0;
    e->header.externalFileAttributes = 0x20; // whatever this is
    e->header.relativeOffsetOflocalHeader = ftell(z->out);

    if(!compress_level) goto dont_compress;

    // Read whole file and and use compress(). Simple but won't handle GB files well.
    unsigned dataSize = e->header.uncompressedSize, compSize = BOUNDS(e->header.uncompressedSize, compress_level);
    void *comp = 0, *data = 0;

    comp = REALLOC(0, compSize);
    if(comp == NULL) goto cant_compress;

    data = REALLOC(0, dataSize);
    if(data == NULL) goto cant_compress;

    fseek(in, 0, SEEK_SET); // rewind
    size_t bytes = fread(data, 1, dataSize, in);
    if(bytes != dataSize) {
        return ERR(false, "Failed to read file in full (%lu vs. %ld bytes)", (unsigned long)bytes, dataSize);
    }

    compSize = COMPRESS(data, (unsigned)dataSize, comp, (unsigned)compSize, compress_level);
    if(!compSize) goto cant_compress;
    if(compSize >= (dataSize * 0.98) ) goto dont_compress;

    uint16_t cl = 8 | (compress_level > 10 ? compress_level << 8 : 0);
    e->header.compressedSize = compSize;
    e->header.compressionMethod = cl;
    goto common;

cant_compress:
dont_compress:;
    e->header.compressedSize = ftell(in);
    e->header.compressionMethod = 0; // store method

common:;
    // write local header
    uint32_t signature = 0x04034B50;
    fwrite(&signature, 1, sizeof(signature), z->out);
    fwrite(&(e->header.versionNeededToExtract), 1, sizeof_JZLocalFileHeader - sizeof(signature), z->out);
    // write filename
    fwrite(entryname, 1, strlen(entryname), z->out);

    if(e->header.compressionMethod) {
        // store compressed blob
        fwrite(comp, compSize, 1, z->out);
    } else {
        // store uncompressed blob
        fseek(in, 0, SEEK_SET);
        while(!feof(in) && !ferror(in)) {
            size_t bytes = fread(buf, 1, sizeof(buf), in);
            fwrite(buf, 1, bytes, z->out);
        }
    }

    REALLOC(comp, 0);
    REALLOC(data, 0);
    return true;
}

// zip common

zip* zip_open(const char *file, const char *mode /*r,w,a*/) {
    struct stat buffer;
    int exists = (stat(file, &buffer) == 0);
    if( mode[0] == 'a' && !exists ) mode = "wb";
    FILE *fp = fopen(file, mode[0] == 'w' ? "wb" : mode[0] == 'a' ? "a+b" : "rb");
    if( !fp ) return ERR(NULL, "cannot open file for %s mode", mode);
    zip zero = {0}, *z = (zip*)REALLOC(0, sizeof(zip));
    if( !z ) return ERR(NULL, "out of mem"); else *z = zero;
    if( mode[0] == 'w' ) {
        z->out = fp;
        return z;
    }
    if( mode[0] == 'r' || mode[0] == 'a' ) {
        z->in = fp;

        JZEndRecord jzEndRecord = {0};
        if(jzReadEndRecord(fp, &jzEndRecord) != JZ_OK) {
            REALLOC(z, 0);
            return ERR(NULL, "Couldn't read ZIP file end record.");
        }
        if(jzReadCentralDirectory(fp, &jzEndRecord, zip__callback, z) != JZ_OK) {
            REALLOC(z, 0);
            return ERR(NULL, "Couldn't read ZIP file central directory.");
        }
        if( mode[0] == 'a' ) {

            // resize (by truncation)
            size_t resize = jzEndRecord.centralDirectoryOffset;
            int fd = fileno(fp);
            if( fd != -1 ) {
                #ifdef _WIN32
                    int ok = 0 == _chsize_s( fd, resize );
                #else
                    int ok = 0 == ftruncate( fd, (off_t)resize );
                #endif
                fflush(fp);
                fseek( fp, 0L, SEEK_END );
            }

            z->in = NULL;
            z->out = fp;
        }
        return z;
    }
    REALLOC(z, 0);
    return ERR(NULL, "Unknown open mode %s", mode);
}

void zip_close(zip* z) {
    if( z->out && z->count ) {
        // prepare end record
        JZEndRecord end = {0};
        end.signature = 0x06054b50;
        end.diskNumber = 0;
        end.centralDirectoryDiskNumber = 0;
        end.numEntriesThisDisk = z->count;
        end.numEntries = z->count;
        end.centralDirectoryOffset = ftell(z->out);
        // flush global directory: global file+filename each
        {
            unsigned i;
            for (i = 0; i < z->count; i++) {
                struct zip_entry *h = &z->entries[i];
                JZGlobalFileHeader *g = &h->header;
                fwrite(g, 1, sizeof(JZGlobalFileHeader), z->out);
                fwrite(h->filename, 1, g->fileNameLength, z->out);
                fwrite(h->extra, 1, g->extraFieldLength, z->out);
                fwrite(h->comment, 1, g->fileCommentLength, z->out);
            }
        }
        end.centralDirectorySize = ftell(z->out) - end.centralDirectoryOffset;
        end.zipCommentLength = 0;

        // flush end record
        fwrite(&end, 1, sizeof(end), z->out);
    }
    if( z->out ) fclose(z->out);
    if( z->in ) fclose(z->in);
    // clean up
    {
        unsigned i;
        for (i = 0; i < z->count; ++i) {
            REALLOC(z->entries[i].filename, 0);
            if (z->entries[i].extra) REALLOC(z->entries[i].extra, 0);
            if (z->entries[i].comment) REALLOC(z->entries[i].comment, 0);
        }
    }
    if(z->entries) REALLOC(z->entries, 0);
    zip zero = {0}; *z = zero; REALLOC(z, 0);
}

#endif // ZIP_C

#line 1 "src/tar.c" 
// gnu tar and ustar extraction
// - rlyeh, public domain.

#ifndef TAR_H
#define TAR_H

typedef struct tar tar;

tar *tar_open(const char *filename, const char *mode);

    unsigned int tar_find(tar*, const char *entryname); // returns entry number; or <0 if not found.
    unsigned tar_count(tar*);
        char*    tar_name(tar*, unsigned index);
        unsigned tar_size(tar*, unsigned index);
        unsigned tar_offset(tar*, unsigned index);
        void*    tar_extract(tar*, unsigned index); // must free() after use

void tar_close(tar *t);

#endif

// -----------------------------------------------------------------------------

#ifdef TAR_C
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef STRDUP
#define STRDUP strdup
#endif

#ifndef REALLOC
#define REALLOC realloc
#endif

#ifndef ERR
#define ERR(NUM, ...) (fprintf(stderr, "" __VA_ARGS__), fprintf(stderr, "(%s:%d)\n", __FILE__, __LINE__), fflush(stderr), (NUM)) // (NUM)
#endif

struct tar {
    FILE *in;
    unsigned count;
    struct tar_entry {
    char *filename;
    unsigned size;
    size_t offset;
    } *entries;
};

// equivalent to sscanf(buf, 8, "%.7o", &size); or (12, "%.11o", &modtime)
// ignores everything after first null or space, including trailing bytes
uint64_t tar__octal( const char *src, const char *eof ) {
    uint64_t sum = 0, mul = 1;
    const char *ptr = eof;
    while( ptr-- >= src ) eof  = ( 0 != ptr[1] && 32 != ptr[1] ) ? eof : ptr;
    while( eof-- >= src ) sum += (uint8_t)(eof[1] - '0') * mul, mul *= 8;
    return sum;
}

typedef int (*tar_callback)(const char *filename, unsigned inlen, size_t offset, void *userdata);

int tar__push_entry(const char *filename, unsigned inlen, size_t offset, void *userdata) {
    tar *t = (tar *)userdata;

    unsigned index = t->count;
    t->entries = REALLOC(t->entries, (++t->count) * sizeof(struct tar_entry));
    struct tar_entry *e = &t->entries[index];

    e->filename = STRDUP(filename);
    e->size = inlen;
    e->offset = offset;

    return 1;
}

int tar__parse( FILE *in, tar_callback yield, void *userdata ) {
    enum {
        name     =   0, // (null terminated)
        mode     = 100, // (octal)
        uid      = 108, // (octal)
        gid      = 116, // (octal)
        size     = 124, // (octal)
        modtime  = 136, // (octal)
        checksum = 148, // (octal)
        type     = 156, // \0|'0':file,1:hardlink,2:symlink,3:chardev,4:blockdev,5:dir,6:fifo,L:longnameblocknext
        linkname = 157, // if !ustar link indicator
        magic    = 257, // if ustar "ustar" -- 6th character may be space or null, else zero
        version  = 263, // if ustar "00", else zero
        uname    = 265, // if ustar owner username, else zero
        gname    = 297, // if ustar owner groupname, else zero
        devmajor = 329, // if ustar device major number, else zero
        devminor = 337, // if ustar device minor number , else zero
        path     = 345, // if ustar filename prefix, else zero
        padding  = 500, // if ustar relevant for checksum, else zero
        total    = 512
    };
    // handle both regular tar and ustar tar filenames until end of tar is found
    char header[512], entry[512], blank[512] = {0};
    while( !ferror(in) ) {
        if( 512 != fread(header, 1, 512, in ) ) break;
        if( memcmp( header, blank, 512 ) ) {                                      // if not end of tar
            if( !memcmp( header+magic, "ustar", 5 ) ) {                           // if valid ustar
                int namelen = strlen(header+name), pathlen = strlen(header+path); // read filename
                snprintf(entry, 512, "%.*s" "%s" "%.*s",
                    pathlen < 155 ? pathlen : 155, header+path,
                    pathlen ? "/" : "",
                    namelen < 100 ? namelen : 100, header+name );
                switch( header[type] ) {
                    default:                                                      // unsupported file type
                    break; case '5': //yield(entry.back()!='/'?entry+'/':entry,0);// directory
                    break; case 'L': strcpy(entry, header+name); fread(header,1,512,in); // gnu tar long filename
                    break; case '0': case 0: {                                    // regular file
                        uint64_t len = tar__octal(header+size, header+modtime);    // decode octal size
                        int cont = yield(entry, len, ftell(in), userdata);        // yield entry
                        fseek(in,len,SEEK_CUR);                                   // skip blob
                        fseek(in,(512 - (len & 511)) & 511,SEEK_CUR);             // skip padding
                    }
                }
            } else return ERR(0, "not a .tar file");
        } else return ferror(in) ? ERR(0, "file error") : 1;
    }
    return ERR(0, "read error");
}

// ---

tar *tar_open(const char *filename, const char *mode) {
    if(mode[0] != 'r') return ERR(NULL, "(w) and (a) not supported for now");
    FILE *in = fopen(filename, "rb");
    if(!in) return ERR(NULL, "cant open file '%s' for reading", filename);

    tar zero = {0}, *t = REALLOC(0, sizeof(tar));
    if( !t ) { fclose(in); return ERR(NULL, "out of mem"); }

    *t = zero;
    t->in = in;
    tar__parse(in, tar__push_entry, t);
    return t;
}

unsigned int tar_find(tar *t, const char *entryname) {
    if( t->in ) {
        unsigned i;
        for ( i = t->count; --i >= 0; ) { // in case of several copies, grab most recent file (last coincidence)
            if (0 == strcmp(entryname, t->entries[i].filename)) return i;
        }
    }
    return -1;
}

unsigned int tar_count(tar *t) {
    return t ? t->count : 0;
}

char* tar_name(tar *t, unsigned index) {
    return t && index < t->count ? t->entries[index].filename : 0;
}

unsigned tar_size(tar *t, unsigned index) {
    return t && index < t->count ? t->entries[index].size : 0;
}

unsigned tar_offset(tar *t, unsigned index) {
    return t && index < t->count ? (unsigned)t->entries[index].offset : 0;
}

void *tar_extract(tar *t, unsigned index) {
    if( t && index < t->count ) {
        fseek(t->in, t->entries[index].offset, SEEK_SET);
        size_t len = t->entries[index].size;
        void *data = REALLOC(0, t->entries[index].size);
        fread(data, 1, len, t->in);
        return data;
    }
    return 0;
}

void tar_close(tar *t) {
    fclose(t->in);
    {
        unsigned i;
        for ( i = 0; i < t->count; ++i ) {
            REALLOC(t->entries[i].filename, 0);
        }
    }
    {
        tar zero = {0};
        *t = zero;
        REALLOC(t, 0);
    }
}

#ifdef TAR_DEMO
int main( int argc, char **argv ) {
    if(argc <= 1) exit(printf("%s file.tar [file_to_view]\n", argv[0]));
    tar *t = tar_open(argv[1], "rb");
    if( t ) {
        unsigned i;
        for( i = 0; i < tar_count(t); ++i ) {
            printf("%d) %s (%u bytes)\n", i+1, tar_name(t,i), tar_size(t,i));
            char *data = tar_extract(t,i);
            if(argc>2) if(0==strcmp(argv[2],tar_name(t,i))) printf("%.*s\n", tar_size(t,i), data);
            free(data);
        }
        tar_close(t);
    }
}
#define main main__
#endif //TAR_DEMO
#endif //TAR_C

#line 1 "src/pak.c" 
// pak file reading/writing/appending.
// - rlyeh, public domain.
//
// ## PAK
// - [ref] https://quakewiki.org/wiki/.pak (public domain).
// - Header: 12 bytes
//   - "PACK"           4-byte
//   - directory offset uint32
//   - directory size   uint32 (number of files by dividing this by 64, which is sizeof(pak_entry))
//
// - File Directory Entry (Num files * 64 bytes)
//   - Each Directory Entry: 64 bytes
//     - file name     56-byte null-terminated string. Includes path. Example: "maps/e1m1.bsp".
//     - file offset   uint32 from beginning of pak file.
//     - file size     uint32

#ifndef PAK_H
#define PAK_H

typedef struct pak pak;

pak* pak_open(const char *fname, const char *mode /*a,r,w*/);

    // (w)rite or (a)ppend modes only
    int pak_append_file(pak*, const char *filename, FILE *in);
    int pak_append_data(pak*, const char *filename, const void *in, unsigned inlen);
    
    // (r)ead only mode
    int pak_find(pak*,const char *fname); // return <0 if error; index otherwise.
    unsigned pak_count(pak*);
        unsigned pak_size(pak*,unsigned index);
        unsigned pak_offset(pak*, unsigned index);
        char *pak_name(pak*,unsigned index);
        void *pak_extract(pak*, unsigned index); // must free() after use

void pak_close(pak*);

#endif

// ---

#ifdef PAK_C
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef REALLOC
#define REALLOC realloc
#endif

#ifndef ERR
#define ERR(NUM, ...) (fprintf(stderr, "" __VA_ARGS__), fprintf(stderr, "(%s:%d)\n", __FILE__, __LINE__), fflush(stderr), (NUM)) // (NUM)
#endif

#include <stdint.h>
static inline uint32_t pak_swap32( uint32_t t ) { return (t >> 24) | (t << 24) | ((t >> 8) & 0xff00) | ((t & 0xff00) << 8); }

#if defined(_M_IX86) || defined(_M_X64) // #ifdef LITTLE
#define htob32(x) pak_swap32(x)
#define btoh32(x) pak_swap32(x)
#define htol32(x) (x)
#define ltoh32(x) (x)
#else
#define htob32(x) (x)
#define btoh32(x) (x)
#define htol32(x) pak_swap32(x)
#define ltoh32(x) pak_swap32(x)
#endif

#pragma pack(push, 1)

typedef struct pak_header {
    char id[4];
    uint32_t offset;
    uint32_t size;
} pak_header;

typedef struct pak_file {
    char name[56];
    uint32_t offset;
    uint32_t size;
} pak_file;

#pragma pack(pop)

typedef int static_assert_sizeof_pak_header[sizeof(pak_header) == 12];
typedef int static_assert_sizeof_pak_file[sizeof(pak_file) == 64];

typedef struct pak {
    FILE *in, *out;
    int dummy;
    pak_file *entries;
    unsigned count;
} pak;

pak *pak_open(const char *fname, const char *mode) {
    struct stat buffer;
    int exists = (stat(fname, &buffer) == 0);
    if(mode[0] == 'a' && !exists ) mode = "wb";

    if(mode[0] != 'w' && mode[0] != 'r' && mode[0] != 'a') return NULL;

    FILE *fp = fopen(fname, mode[0] == 'w' ? "wb" : mode[0] == 'r' ? "rb" : "r+b");
    if(!fp) return ERR(NULL, "cant open file '%s' in '%s' mode", fname, mode);

    pak *p = malloc(sizeof(pak)), zero = {0};
    if(!p) return fclose(fp), ERR(NULL, "out of mem");
    *p = zero;

    if( mode[0] == 'r' || mode[0] == 'a' ) {
        pak_header header = {0};

        if( fread(&header, 1, sizeof(pak_header), fp) != sizeof(pak_header) ) {
            return fclose(fp), ERR(NULL, "read error");
        }
        if( memcmp(header.id, "PACK", 4) ) {
            return fclose(fp), ERR(NULL, "not a .pak file");
        }

        header.offset = ltoh32(header.offset);
        header.size = ltoh32(header.size);

        unsigned num_files = header.size / sizeof(pak_file);

        if( fseek(fp, header.offset, SEEK_SET) != 0 ) {
            return fclose(fp), ERR(NULL, "read error");
        }

        p->count = num_files;
        p->entries = REALLOC(0, num_files * sizeof(pak_file));

        if( fread(p->entries, num_files, sizeof(pak_file), fp) != sizeof(pak_file) ) {
            goto fail;
        }

        {
            unsigned i;
            for ( i = 0; i < num_files; ++i ) {
                pak_file *e = &p->entries[i];
                e->offset = ltoh32(e->offset);
                e->size = ltoh32(e->size);
            }
        }

        if( mode[0] == 'a' ) {
            // resize (by truncation)
            size_t resize = header.offset;
            int fd = fileno(fp);
            if( fd != -1 ) {
                #ifdef _WIN32
                    int ok = 0 == _chsize_s( fd, resize );
                #else
                    int ok = 0 == ftruncate( fd, (off_t)resize );
                #endif
                fflush(fp);
                fseek( fp, 0L, SEEK_END );
            }

            p->out = fp;
            p->in = NULL;
        } else {
            p->in = fp;
        }

        return p;
    }


    if(mode[0] == 'w') {
        p->out = fp;

        // write temporary header
        char header[12] = {0};
        if( fwrite(header, 1,12, p->out) != 12) goto fail;

        return p;
    }

fail:;
    if(fp) fclose(fp);
    if(p->entries) REALLOC(p->entries, 0);
    if(p) REALLOC(p, 0);

    return NULL;
}

int pak_append_data(pak *p, const char *filename, const void *in, unsigned inlen) {
    if(!p->out) return ERR(0, "read-only pak file");

    // index meta
    unsigned index = p->count++;
    p->entries = REALLOC(p->entries, p->count * sizeof(pak_file));
    pak_file *e = &p->entries[index], zero = {0};
    *e = zero;
    snprintf(e->name, 55, "%s", filename); // @todo: verify 56 chars limit
    e->size = inlen;
    e->offset = ftell(p->out);

    // write blob
    fwrite(in, 1, inlen, p->out);

    return !ferror(p->out);
}

int pak_append_file(pak *p, const char *filename, FILE *in) {
    // index meta
    unsigned index = p->count++;
    p->entries = REALLOC(p->entries, p->count * sizeof(pak_file));
    pak_file *e = &p->entries[index], zero = {0};
    *e = zero;
    snprintf(e->name, 55, "%s", filename); // @todo: verify 56 chars limit
    e->offset = ftell(p->out);

    char buf[1<<15];
    while(!feof(in) && !ferror(in)) {
        size_t bytes = fread(buf, 1, sizeof(buf), in);
        fwrite(buf, 1, bytes, p->out);
    }

    e->size = ftell(p->out) - e->offset;

    return !ferror(p->out);
}


void pak_close(pak *p) {
    if(p->out) {
        // write toc
        uint32_t seek = 0 + 12, dirpos = (uint32_t)ftell(p->out), dirlen = p->count * 64;
        {
            unsigned i;
            for ( i = 0; i < p->count; ++i ) {
                pak_file *e = &p->entries[i];
                // write name (truncated if needed), and trailing zeros
                char zero[56] = {0};
                int namelen = strlen(e->name);
                fwrite(e->name, 1, namelen >= 56 ? 55 : namelen, p->out);
                fwrite(zero, 1, namelen >= 56 ? 1 : 56 - namelen, p->out);
                // write offset + length pair
                uint32_t pseek = htol32(seek);
                fwrite(&pseek, 1, 4, p->out);
                uint32_t psize = htol32(e->size);
                fwrite(&psize, 1, 4, p->out);
                seek += e->size;
            }
        }

        // patch header
        fseek(p->out, 0L, SEEK_SET);
        fwrite("PACK", 1,4, p->out);
        dirpos = htol32(dirpos); fwrite( &dirpos, 1,4, p->out );
        dirlen = htol32(dirlen); fwrite( &dirlen, 1,4, p->out );
    }

    // close streams
    if(p->in) fclose(p->in);
    if(p->out) fclose(p->out);

    // clean up
    {
        unsigned i;
        for ( i = 0; i < p->count; ++i ) {
            pak_file *e = &p->entries[i];
        }
    }
    REALLOC(p->entries, 0);

    // delete
    pak zero = {0};
    *p = zero;
    REALLOC(p, 0);
}

int pak_find(pak *p, const char *filename) {
    if( p->in ) {
        unsigned i;
        for( i = p->count; --i >= 0; ) {
            if(!strcmp(p->entries[i].name, filename)) return i;
        }
    }
    return -1;
}

unsigned int pak_count(pak *p) {
    return p->in ? p->count : 0;
}

unsigned int pak_offset(pak *p, unsigned index) {
    return p->in && index < p->count ? p->entries[index].offset : 0;
}

unsigned pak_size(pak *p, unsigned index) {
    return p->in && index < p->count ? p->entries[index].size : 0;
}

char *pak_name(pak *p, unsigned index) {
    return p->in && index < p->count ? p->entries[index].name : NULL;
}

void *pak_extract(pak *p, unsigned index) {
    if( p->in && index < p->count ) {
        pak_file *e = &p->entries[index];
        if( fseek(p->in, e->offset, SEEK_SET) != 0 ) {
            return ERR(NULL, "cant seek");
        }
        void *buffer = REALLOC(0, e->size);
        if( !buffer ) {
            return ERR(NULL, "out of mem");
        }
        if( fread(buffer, 1, e->size, p->in) != e->size ) {
            REALLOC(buffer, 0);
            return ERR(NULL, "cant read");
        }
        return buffer;
    }
    return NULL;
}

#ifdef PAK_DEMO
int main(int argc, char **argv) {
    puts("creating test.pak archive (3) ...");
    pak *p = pak_open("test.pak", "wb");
    if( p ) {
        pak_append_data(p, "/index.txt", "just a file", strlen("just a file"));
        pak_append_data(p, "/file/name1.txt", "just another file #1", strlen("just another file #1"));
        pak_append_data(p, "/file/name2.txt", "just another file #2", strlen("just another file #2"));
        pak_close(p);
    }

    puts("appending file to test.pak (1) ...");
    p = pak_open("test.pak", "a+b");
    if( p ) {
        pak_append_data(p, "/new/file", "this is an appended file", strlen("this is an appended file"));
        pak_close(p);
    }

    const char *fname = argc > 1 ? argv[1] : "test.pak";
    printf("listing %s archive ...\n", fname);
    p = pak_open(fname, "rb");
    if( p ) {
        unsigned i;
        for( i = 0; i < pak_count(p); ++i ) {
            printf("  %d) @%08x %11u %s ", i+1, pak_offset(p,i), pak_size(p,i), pak_name(p,i));
            void *data = pak_extract(p,i);
            printf("\r%c\n", data ? 'Y':'N');
            if(argc > 2 && data) 
                if(i == pak_find(p,argv[2]))
                    printf("%.*s\n", (int)pak_size(p,i), (char*)data);
            free(data);
        }
        pak_close(p);
    }

    puts("ok");
}
#endif // PAK_DEMO
#endif // PAK_C

#line 1 "src/vfs.c" 
// virtual filesystem (registered directories and/or compressed zip archives).
// - rlyeh, public domain.
//
// - note: vfs_mount() order matters (the most recent the higher priority).

void  vfs_mount(const char *path); // zipfile or directory/with/trailing/slash/
char* vfs_load(const char *filename, unsigned int *size); // must free() after use

// -----------------------------------------------------------------------------

#ifdef VFS_C
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static
char *vfs_read_file(const char *filename, int *len) {
    FILE *fp = fopen(filename, "rb");
    if( fp ) {
        fseek(fp, 0L, SEEK_END);
        size_t sz = ftell(fp);
        fseek(fp, 0L, SEEK_SET);
        char *bin = REALLOC(0, sz+1);
        fread(bin,sz,1,fp);
        fclose(fp);
        bin[sz] = 0;
        if(len) *len = (int)sz;
        return bin;
    }
    return 0;
}

typedef struct vfs_dir {
    char* path;
    // const 
    zip* archive;
    int is_directory;
    struct vfs_dir *next;
} vfs_dir;

static vfs_dir *dir_head = NULL;

void vfs_mount(const char *path) {
    zip *z = NULL;
    int is_directory = ('/' == path[strlen(path)-1]);
    if( !is_directory ) z = zip_open(path, "rb");
    if( !is_directory && !z ) return;

    vfs_dir *prev = dir_head, zero = {0};
    *(dir_head = REALLOC(0, sizeof(vfs_dir))) = zero;
    dir_head->next = prev;
    dir_head->path = STRDUP(path);
    dir_head->archive = z;
    dir_head->is_directory = is_directory;
}

char *vfs_load(const char *filename, unsigned int *size) { // must free() after use
    char *data = NULL;
    vfs_dir *dir;
    for( dir = dir_head; dir && !data; dir = dir->next ) {
        if( dir->is_directory ) {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s%s", dir->path, filename);
            data = vfs_read_file(buf, size);
        } else {
            unsigned int index = zip_find(dir->archive, filename);
            data = zip_extract(dir->archive, index);
            if( size ) *size = zip_size(dir->archive, index);
        }
        // printf("%c trying %s in %s ...\n", data ? 'Y':'N', filename, dir->path);
    }
    return data;
}

#ifdef VFS_DEMO
int main() {
    vfs_mount("../src/"); // directories/must/end/with/slash/
    vfs_mount("demo.zip"); // zips supported
    printf("vfs.c file found? %s\n", vfs_load("vfs.c", 0) ? "Y":"N"); // should be Y
    printf("stdarc.c file found? %s\n", vfs_load("stdarc.c", 0) ? "Y":"N"); // should be N
    printf("demo_zip.c file found? %s\n", vfs_load("demo_zip.c", 0) ? "Y":"N"); // should be Y after running demo_zip.exe
}
#define main main__
#endif // VFS_DEMO
#endif // VFS_C

#line 1 "src/dir.c" 
// directory iteration.
// - rlyeh, public domain.

#ifndef DIR_H
#define DIR_H

typedef struct dir dir;

dir *dir_open(const char *filename, const char *mode); // recursive 'r'

    int dir_find(dir*, const char *entryname); // returns entry number; or <0 if not found.
    unsigned dir_count(dir*);
        char*    dir_name(dir*, unsigned index);
        unsigned dir_size(dir*, unsigned index);
        unsigned dir_file(dir*, unsigned index); // dir_isfile? bool?
        void*    dir_read(dir*, unsigned index); // must free() after use

void dir_close(dir*);

#endif

// -----------------------------------------------------------------------------

#ifdef DIR_C
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#  if defined _WIN32 && defined(__TINYC__)
#include <windows.h>  // tcc
#elif defined _WIN32
#include <winsock2.h> // msc+gcc
#else
#include <dirent.h>
#endif

#ifndef STRDUP
#define STRDUP strdup
#endif

#ifndef REALLOC
#define REALLOC realloc
#endif

#ifndef ERR
#define ERR(NUM, ...) (fprintf(stderr, "" __VA_ARGS__), fprintf(stderr, "(%s:%d)\n", __FILE__, __LINE__), fflush(stderr), (NUM)) // (NUM)
#endif

typedef struct dir_entry {
    char *filename;
    size_t size;
    size_t is_dir : 1;
} dir_entry;

struct dir {
    dir_entry *entry;
    unsigned count;
};

// ---

#if !defined(S_ISDIR)
#   define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

int dir_yield(dir *d, const char *pathfile, char *name, int namelen) {
    int ok = 0;
#ifdef _WIN32
    WIN32_FIND_DATAA fdata = { 0 };
    snprintf(name, namelen, "%s/*", pathfile);
    {
        HANDLE h;
        for( h = FindFirstFileA(name, &fdata ); h != INVALID_HANDLE_VALUE; (ok = FindClose( h ), h = INVALID_HANDLE_VALUE, 1) ) {
            int next;
            for( next = 1; next; next = FindNextFileA(h, &fdata) != 0 ) {
                if( fdata.cFileName[0] == '.' ) continue;
                int is_dir = (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) > 0;
                snprintf(name, namelen, "%s/%s%s", pathfile, fdata.cFileName, is_dir ? "/" : "");
                struct stat st; if( !is_dir ) if(stat(name, &st) < 0) continue;
                // add
                dir_entry de = { STRDUP(name), is_dir ? 0 : st.st_size, is_dir };
                d->entry = (dir_entry*)REALLOC(d->entry, ++d->count * sizeof(dir_entry));
                d->entry[d->count-1] = de;
            }
        }
    }
#else
    snprintf(name, namelen, "%s/", pathfile);
    DIR *dir;
    for( dir = opendir(name); dir; ok = (closedir(dir), dir = 0, 1) ) {
        struct dirent *ep;
        while( (ep = readdir(dir)) ) {
            if( ep->d_name[0] == '.' ) continue;
            snprintf(name, namelen, "%s/%s", pathfile, ep->d_name);
            struct stat st; if( stat(name, &st) < 0 ) continue;
            DIR *tmp = opendir(ep->d_name); int is_dir = !!tmp; if(tmp) closedir(tmp);
            // if( is_dir && recursive ) { dir_yield(d,name); }
            // add
            dir_entry de = { STRDUP(name), is_dir ? 0 : st.st_size, is_dir };
            d->entry = (dir_entry*)REALLOC(d->entry, ++d->count * sizeof(dir_entry));
            d->entry[d->count-1] = de;
        }
    }
#endif
    return ok;
}

dir *dir_open(const char *pathfile, const char *mode) {
    // if(mode[0] == 'R') return ERR(NULL, "(R) not supported for now");
    dir *d = (dir*)REALLOC(0, sizeof(dir)), zero = {0}; *d = zero;

    char *clean = STRDUP( pathfile );
    char buffer[2048];
    {
        int i;
        for (i = 0; clean[i]; ++i) if (clean[i] == '\\') clean[i] = '/';
    }
    {
        size_t len;
        for (len = strlen(clean); clean[--len] == '/';) clean[len] = '\0';
    }

    dir_yield(d, clean, buffer, 2048);

    REALLOC(clean, 0);
    return d;
}

int dir_find(dir *d, const char *entryname) {
    int i;
    for( i = d->count; --i >= 0; ) { // in case of several copies, grab most recent file (last coincidence)
        if( 0 == strcmp(entryname, d->entry[i].filename)) return i;
    }
    return -1;
}

unsigned dir_count(dir *d) {
    return d ? d->count : 0;
}

char* dir_name(dir *d, unsigned index) {
    return d && index < d->count ? d->entry[index].filename : 0;
}

unsigned dir_size(dir *d, unsigned index) {
    return d && index < d->count ? (unsigned)d->entry[index].size : 0;
}

unsigned dir_file(dir *d, unsigned index) {
    return d && index < d->count ? (unsigned)!d->entry[index].is_dir : 0;
}

void *dir_read(dir *d, unsigned index) {
    if( d && index < d->count ) {
        void *data = 0;
        FILE *fp;
        for( fp = fopen(d->entry[index].filename, "rb"); fp; fclose(fp), fp = 0 ) {
            size_t len = d->entry[index].size;
            data = REALLOC(0, len);
            if( data && fread(data, 1, len, fp) != len ) {
                data = REALLOC(data, 0);
            }
        }
        return data;
    }
    return 0;
}

void dir_close(dir *d) {
    int i;
    for( i = 0; i < d->count; ++i ) {
        REALLOC(d->entry[i].filename, 0);
    }
    dir zero = {0};
    *d = zero;
    REALLOC(d, 0);
}

#ifdef DIR_DEMO
int main( int argc, char **argv ) {
    dir *d = dir_open(argc > 1 ? argv[1] : "./", "rb");
    if( d ) {
        int i;
        for( i = 0; i < dir_count(d); ++i ) {
            if( dir_file(d,i) )
            printf("%3d) %11d %s\n", i + 1, dir_size(d,i), dir_name(d,i));
            else
            printf("%3d) %11s %s\n", i + 1, "<dir>", dir_name(d,i));
            char *data = dir_read(d,i);
            if(argc > 2 && !strcmp(argv[2],dir_name(d,i))) printf("%.*s\n", dir_size(d,i), data);
            free(data);
        }
        dir_close(d);
    }
}
#define main main__
#endif //DIR_DEMO
#endif //DIR_C
