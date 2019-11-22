#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <mach/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>

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
static int istest;

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


struct aheader
{
unsigned int sync;                        //同步信息 11
unsigned int version;                      //版本 2

unsigned int layer;                           //层 2

unsigned int protection;           // CRC校验 1

unsigned int bitrate_index;              //位率 4

unsigned int sampling_frequency;         //采样频率 2

unsigned int padding;                    //帧长调节 1

unsigned int private;                       //保留字 1

unsigned int mode;                         //声道模式 2

unsigned int modeextension;        //扩充模式 2

unsigned int copyright;                           // 版权 1

unsigned int original;                      //原版标志 1

unsigned int emphasis;                  //强调模式 2
};

void printmp3details(unsigned int nFrames, unsigned int nSampleRate, double fAveBitRate)
{
    printf("MP3 details:\n");
    printf("Frames: %d\n", nFrames);
    printf("Sample rate: %d\n", nSampleRate);
    printf("Ave bitrate: %0.0f\n", fAveBitRate);
}

int findFramePadding (const unsigned char ucHeaderByte)
{
    //get second to last bit to of the byte
    unsigned char ucTest = ucHeaderByte & 0x02;
    //this is then a number 0 to 15 which correspond to the bit rates in the array
    int nFramePadded;
    if( (unsigned int)ucTest==2 )
    {
        nFramePadded = 1;
        printf("\tpadded: true");
    }
    else
    {
        nFramePadded = 0;
        printf("\tpadded: false");
    }
    return nFramePadded;
}

int findMpegVersionAndLayer (const unsigned char ucHeaderByte)
{
    int MpegVersionAndLayer;
    //get bits corresponding to the MPEG verison ID and the Layer
    unsigned char ucTest = ucHeaderByte & 0x1E;
    //we are working with MPEG 1 and Layer III
    if(ucTest == 0x1A)
    {
        MpegVersionAndLayer = 1;
        printf("\tMPEG Version 1 Layer III ");
    }
    else
    {
        MpegVersionAndLayer = 1;
        printf("\tNot MPEG Version 1 Layer III ");
    }
    return MpegVersionAndLayer;
}

int findFrameBitRate (const unsigned char ucHeaderByte)
{
    unsigned int bitrate[] = {0,32000,40000,48000,56000,64000,80000,96000,
                         112000,128000,160000,192000,224000,256000,320000,0};
    //get first 4 bits to of the byte
    unsigned char ucTest = ucHeaderByte & 0xF0;
    //move them to the end
    ucTest = ucTest >> 4;
    //this is then a number 0 to 15 which correspond to the bit rates in the array
     int unFrameBitRate = bitrate[(unsigned int)ucTest];
    printf("\tBit Rate: %u\n",unFrameBitRate);
    return unFrameBitRate;
}

 int findFrameSamplingFrequency(const unsigned char ucHeaderByte)
{
    unsigned int freq[] = {44100,48000,32000,00000};
    //get first 2 bits to of the byte
    unsigned char ucTest = ucHeaderByte & 0x0C;
    ucTest = ucTest >> 6;
    //then we have a number 0 to 3 corresponding to the freqs in the array
     int unFrameSamplingFrequency = freq[(unsigned int)ucTest];
    printf("Sampling Frequency: %u",unFrameSamplingFrequency);
    return unFrameSamplingFrequency;
}

void printBits(size_t const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j;

    for (i=size-1;i>=0;i--)
    {
        for (j=7;j>=0;j--)
        {
            byte = b[i] & (1<<j);
            byte >>= j;
            printf("%u", byte);
        }
    }
    puts("");
}

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
  printf("TAG size:%d,%d,%d,%d\n",header.size[0], header.size[1],header.size[2],header.size[3]);  
  printf("tag id =%s, tag version = %d.%d\n", header.ID, header.version[0], header.version[1]);
  
  // Flag 取高三位
  unsync = (header.flags & 0x80) ? 1 : 0; //最高位:
  extend = (header.flags & 0x40) ? 1 : 0; //次高位,表示是否有扩展头部 I didn't handle extended in this version
  istest = (header.flags & 0x20) ? 1 : 0;
  printf("unsync = %d, extend = %d, istest = %d\n\n", unsync, extend, istest);

  sz = (header.size[0] & 0x7F) * 0x200000 + (header.size[1] & 0x7F) * 0x4000 + (header.size[2] & 0x7F) * 0x80 + (header.size[3] & 0x7F);
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

int main(int argc, char* argv[])
{
  
  char* tagid[] = { "TIT2", "TALB", "TPE2", "TPE1", "TCON", "TRCK", "TYER", "PRIV", "TCOM", "TCOP", "TEXT", "APIC" };
  char* infoname[] = { "Title", "Album", "Band", "Performer", "Content Type", "Track Number", "Year", "Private Frame", "Composer", "Copyright", "Lyricst", "Picture" };
  
  if (argc < 2) {
    printf("Usage: mp3edit mp3file1 mp3file2...\n");
    return -1;
  }
  int fd;
  if ((fd = open(argv[1], O_RDONLY)) < 0) {
    perror("Open File: ");
    return -1;
  }
  //printf("File opened\n");
  struct stat stbuf;
  if ((fstat(fd, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
      /* Handle error */
    }
      
  int file_size = stbuf.st_size;
  printf("file size = %d\n", file_size);

  size_t tagsize = gettagsize(fd);
  if (tagsize == -1) {            // The file is not an valid ID3 TAG mp3 file
    parseold(fd);                 // assume it's the old style mp3 file, the last 128 bytes hold information
    printf("has no ID3\n");
    close(fd);
    return -1;
  }
  if (tagsize == -2) {
    printf("ID3 but not v2.3\n");
    close(fd);
    return -1;
  }
  printf("tagsize = %d\n\n", tagsize);
  struct frameheader header;
  int framesz = 0;

  int start = 0;
  while(start <= tagsize){
    if (read(fd, &header, sizeof(header)) < 0) {
      perror("Read File: ");
      return -1;
    }

    start += sizeof(header);
    if(start > tagsize){
      start -= sizeof(header);
      printf("\nhas parsed all\n");
      break;
    }
    //(header.size[0] & 0x7F) * 0x200000 + (header.size[1] & 0x7F) * 0x4000 + (header.size[2] & 0x7F) * 0x80 + (header.size[3] & 0x7F);
    //framesz = header.size[0]*0x100000000 + header.size[1]*0x10000 + header.size[2]*0x100 + header.size[3];
    framesz = header.size[0]*0x200000 + header.size[1]*0x4000 + header.size[2]*0x80 + header.size[3];
    start+=framesz;

    char* input = malloc(framesz);
    if(read(fd, input, framesz)< 0){
      printf("error");
      return -1;
    }
    int hasFound = 0;
    for(int i = 0; i < 12; i++){
      if(strncmp(header.frameid, tagid[i], 4) == 0){
        hasFound = 1;
      }
    }
    if(!hasFound){
      break;
    }
    if(strncmp(header.frameid, "APIC", 4) == 0){
      //printf("APIC:%d,%d,%d,%d\n",header.size[0], header.size[1],header.size[2],header.size[3]);
      printf("The %s is:\t\tAttach Picture, size = %d\n", header.frameid, framesz);
      continue;
    }
    int tempSize = framesz;
    framesz -= 1;   // framesz include the encode of the ID, so minus the encode byte; depend on the type of the tagid.
    if (framesz <= 0) continue;
    char* encode = (*input == 1)? "UTF-16" : "GB18030";  // The biggest problem is here, hard to know encode,so just guess

    size_t outsize = framesz * 2;
    char* result = malloc(outsize);
    bzero(result, outsize);

    char *temp = input+1;
    if (strncmp(header.frameid, "PRIV", 4) == 0) {   // PRIV's handle is a bit of special, still don't understand 
      encode = "ISO-8859-1";
      temp = input;
      framesz +=1;
    }
  
    if ((doconv(temp, framesz, encode, result, outsize)) == -1) {
      perror("doconv: ");
      continue;
    }
    printf("The %s is:\t\t%s, size = %d\n", header.frameid, result, tempSize);
    free(result);
  }
  printf("start is %d\n", start);

  int position = lseek(fd, tagsize-10, 0);
  printf("seek to get tag footer position = %d\n", position);

  struct tagheader tagheader;
  size_t sz;

  if (read(fd, &tagheader, sizeof(tagheader)) < 0) {
    perror("Read File: ");
    exit(1);
  }

  if (strncmp(tagheader.ID, "3DI", 3) != 0) {
    printf("not tag footer\n");
    printf("data is %d,%d,%d\n",tagheader.ID[0],tagheader.ID[1],tagheader.ID[2]);
  }else{
    printf("TAG footer size:%d,%d,%d,%d\n",tagheader.size[0], tagheader.size[1],tagheader.size[2],tagheader.size[3]);  
    printf("tag footer id =%s, tag version = %d.%d\n", tagheader.ID, tagheader.version[0], tagheader.version[1]);
  }
  
  position = lseek(fd, tagsize-10, 0);
  printf("seek to get audio frame , position = %d\n", position);

  int nFrames, nFileSampleRate;
  unsigned char ucHeaderByte1, ucHeaderByte2, ucHeaderByte3, ucHeaderByte4;
  float fBitRateSum=0;
syncWordSearch:
  while( position < file_size)
  {
    if (read(fd, &ucHeaderByte1, sizeof(ucHeaderByte1)) < 0) {
      perror("Read File: ");
      exit(1);
    }
    position ++;
    //printf("111:%d\n", ucHeaderByte1);
    if( ucHeaderByte1 == 0xFF )
    {
      if (read(fd, &ucHeaderByte2, sizeof(ucHeaderByte2)) < 0) {
        perror("Read File: ");
        exit(1);
      }
      position ++;
      unsigned char ucByte2LowerNibble = ucHeaderByte2 & 0xF0;
      if( ucByte2LowerNibble == 0xF0 || ucByte2LowerNibble == 0xE0 )
      {
          ++nFrames;
          printf("Found frame %d at offset = %ld B\n",nFrames, position);
          //printf("Header Bits:\n");
          //get the rest of the header:
          if (read(fd, &ucHeaderByte3, sizeof(ucHeaderByte3)) < 0) {
            perror("Read File: ");
            exit(1);
          }
          position ++;
          if (read(fd, &ucHeaderByte4, sizeof(ucHeaderByte4)) < 0) {
            perror("Read File: ");
            exit(1);
          }
          position ++;
          //print the header:
          //printBits(sizeof(ucHeaderByte1),&ucHeaderByte1);
          //printBits(sizeof(ucHeaderByte2),&ucHeaderByte2);
          //printBits(sizeof(ucHeaderByte3),&ucHeaderByte3);
          //printBits(sizeof(ucHeaderByte4),&ucHeaderByte4);
          //get header info:
          int nFrameSamplingFrequency = findFrameSamplingFrequency(ucHeaderByte3);
          int nFrameBitRate = findFrameBitRate(ucHeaderByte3);
          int nMpegVersionAndLayer = findMpegVersionAndLayer(ucHeaderByte2);

          if( nFrameBitRate==0 || nFrameSamplingFrequency == 0 || nMpegVersionAndLayer==0 )
          {//if this happens then we must have found the sync word but it was not actually part of the header
              --nFrames;
              printf("Error: not a header\n\n");
              goto syncWordSearch;
          }
          fBitRateSum += nFrameBitRate;
          if(nFrames==1){ nFileSampleRate = nFrameSamplingFrequency; }
          int nFramePadded = findFramePadding(ucHeaderByte3);
          //calculate frame size:
          int nFrameLength = (144 * (float)nFrameBitRate /
                                            (float)nFrameSamplingFrequency ) + nFramePadded;
          printf("\tFrame Length: %d Bytes \n\n", nFrameLength);

          //lnPreviousFramePosition=ftell(ifMp3)-4; //the position of the first byte of this frame

          //move file position by forward by frame length to bring it to next frame:
          position = lseek(fd, position + nFrameLength-4, 0);
      }
    }
  }
  float fFileAveBitRate= fBitRateSum/nFrames;
  printmp3details(nFrames,nFileSampleRate,fFileAveBitRate);

  close(fd);
}