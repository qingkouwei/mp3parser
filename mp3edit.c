#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <glob.h>
// Many mp3 files format do not conform to the standard, such as the standard says that the encode should be
// ISO-8895-1 if the byte followed that frame header is $00, but in fact, in my system, they are GBK, not
// ISO-8895-1 (YOU SHOULD KNOW THAT I AM A CHINESE :)

/* Format of the mp3 ID3v2.3 */
/* | tagheader(10 bytes) | frames (frameheader information)| expanded header (optional) | mp3 audio data | */

/* Imporatant! : The Bit Order in ID3v2 is most significant bit first (MSB) */

struct mp3id3v1 {
  char header[3];
  char title[30];
  char artist[30];
  char album[30];
  char year[4];
  char comment[30];
  char genre;
};

struct tagheader {
  char ID[3];          // The first 4 bytes should be ID3
  char version[2];     // $03 00
  char flags;          // $abc00000 : a:unsynchronisation if set; b:extended header exist if set; c:experimental indicator if set
  char size[4];        // (total tag size - 10) excluding the tagheader;
};

static int unsync;
static int extend;

struct extheader {
  char extsize[4];
  char extflags[2];    // %x0000000 00000000 , x means if CRC data present, if set crc-32 data is appended to the extended header.
  char padding[4];
};
  
struct frameheader {
  char frameid[4];    // TIT2 MCDI TRCK ...
  char size[4];
  char flags[2];      // %abc00000  %ijk00000 | a 0:frame should be preserved 1:frame should be discard
};                    // b 0:frame should be preserved; 1 otherwise, c:read only
                      // i, 0:frame is not compressed, j:encryption 0:frame is not encrypted
/* If nothing else is said, a string is represented as ISO-8859-1 characters. All unicode strings use 16-bit unicode 2.0
   unicode string must begin with the unicode BOM ($FF FE or $FE FF) to identify the byte order. All numeric strings and URLS are 
   always encoded as ISO-8859-1. Terminated strings are terminated with $00 if encoded with ISO-8859-1 and $00 00 if encoded as unicode */

/* Frames that allow different types of text encoding have a text encoding description byte directly after the frame size. If ISO-8859-1 is
   used, this type should be $00, if unicode is used, it should be $01. */

static size_t gettagsize(int fd)
{
  struct tagheader header;
  size_t sz;

  if (read(fd, &header, sizeof(header)) < 0) {
    perror("Read File: ");
    exit(1);
  }

  if (strncmp(header.ID, "ID3", 3) != 0) {
    return -1;
  }

  if (strncmp(header.version, "\03\00", 2) != 0) {
    return -2;
  }

  unsync = (header.flags & 0x80) ? 1 : 0;
  extend = (header.flags & 0x40) ? 1 : 0;    // I didn't handle extended in this version
  
  sz = (header.size[0] & 0x7F) * 0x200000 + (header.size[1] & 0x7F) * 0x400 + (header.size[2] & 0x7F) * 0x80 + (header.size[3] & 0x7F);
  return sz;
}

static int doconv(char* inbuf, size_t inbytes, char* encode, char* outbuf, size_t outbytes)
{
  iconv_t cd;
  
  if ((cd = iconv_open("UTF-8", encode)) == (iconv_t) -1) {
    perror("Create Iconv: ");
    return -1;
  }

  if ((iconv(cd, &inbuf, &inbytes, &outbuf, &outbytes)) != -1) {
    iconv_close(cd);
    return 1;
  }
  
  iconv_close(cd);
  perror("iconv: ");
  return -1;
}

void parseold(int fd)
{
  struct mp3id3v1 info;
  struct mp3id3v1 result;             // To put the iconv string
  bzero(&info, sizeof(info));
  bzero(&result, sizeof(result));
  lseek(fd, -128, SEEK_END);
  read(fd, &info, sizeof(info));
  if (strncmp(info.header, "TAG", 3) != 0) {
    printf("No TAG ID\n");
    return;
  }

  char* infoname[] = { "Title", "Artist", "Album", "Published year", "Genre" };  // I didn't handle Genre
  char* source[] = { info.title, info.artist, info.album, info.year };
  char* iconvresult[] = { result.title, result.artist, result.album, result.year };

  for (int i = 0; i < 4; i++) {
    if (doconv(source[i], 30, "GB18030", iconvresult[i], 60) == -1) { // someone tell me why the param outbytes must be greater than inbytes
      printf("%s:", infoname[i]);                                     // otherwise iconv complain that: "Argument list too long"
      perror("iconv: ");
      printf("\n");
    } else {
      printf("%s:\t %s\n", infoname[i], iconvresult[i]);
    }
  }
}

void parse(char* filename, char* tag[], char* infoname[], int size)
{
  int fd;
  if ((fd = open(filename, O_RDONLY)) < 0) {
    perror("Open File: ");
    return;
  }
  
  size_t tagsize = gettagsize(fd);
  if (tagsize == -1) {            // The file is not an valid ID3 TAG mp3 file
    parseold(fd);                 // assume it's the old style mp3 file, the last 128 bytes hold information
    close(fd);
    return;
  }
  if (tagsize == -2) {
    printf("ID3 but not v2.3\n");
    close(fd);
    return;
  }
  
  char* frame = NULL;
  struct frameheader* header;
  int framesz = 0;
  
  char* buff = (char *) malloc(tagsize);
  if (read(fd, buff, tagsize) < 0) {
    perror("Read Error: ");
    exit(2);
  }

  for (int i = 0; i < size; i++) {
    if ((frame = (char*) memmem(buff, tagsize, tag[i], 4)) == NULL) {  // memmem(), search substr in memory area
      //      printf("There is no %s\n", infoname[i]);
      continue;
    }
    
    header = (struct frameheader* ) frame;
    
    framesz = header->size[0]*0x100000000 + header->size[1]*0x10000 + header->size[2]*0x100 + header->size[3];
    framesz -= 1;   // framesz include the encode of the ID, so minus the encode byte; depend on the type of the tagid.
    if (framesz <= 0) continue;
    
    char* encode = (*(frame += 10) == 1)? "UTF16" : "GB18030";  // The biggest problem is here, hard to know encode,so just guess
    frame++;

    size_t outsize = framesz * 2;
    char* result = malloc(outsize);
    bzero(result, outsize);

    if (strncmp(tag[i], "PRIV", 4) == 0) {   // PRIV's handle is a bit of special, still don't understand 
      frame--;
      encode = "ISO-8859-1";
    }
    
    if ((doconv(frame, framesz, encode, result, outsize)) == -1) {
      perror("doconv: ");
      continue;
    }
    printf("The %s is:\t\t%s\n", infoname[i], result);
    free(result);
  }
  free(buff);
  close(fd);
}
  
int main(int argc, char* argv[])
{
  glob_t globbuf;
  char* tagid[] = { "TIT2", "TALB", "TPE2", "TPE1", "TCON", "TRCK", "TYER", "PRIV", "TCOM", "TCOP", "TEXT" };
  char* infoname[] = { "Title", "Album", "Band", "Performer", "Content Type", "Track Number", "Year", "Private Frame", "Composer", "Copyright", "Lyricst" };
  
  if (argc < 2) {
    printf("Usage: mp3edit mp3file1 mp3file2...\n");
    return 1;
  }
  
  for (int i = 1; i < argc; i++) {
    if (glob(argv[i], GLOB_NOSORT, NULL, &globbuf) != 0) {
      perror("Glob Error: ");
      continue;
    }
    for (int j = 0; j < globbuf.gl_pathc; j++) {
      printf("-------------------------------------------------------------\n");
      printf("The file name is: %s\n", globbuf.gl_pathv[j]);
      parse(globbuf.gl_pathv[j], tagid, infoname, 11);
    }
  }
  globfree(&globbuf);
}

// To be Continue : The next step is to provide a tag editor.
