/**
* @file sha1.c SHA-1 in C
*/

/*
By Steve Reid <sreid@sea-to-sky.net>
100% Public Domain

-----------------
Modified 7/98
By James H. Brown <jbrown@burgoyne.com>
Still 100% Public Domain

Corrected a problem which generated improper hash values on 16 bit machines
Routine SHA1Update changed from
  void SHA1Update(SHA1_CTX* context, unsigned char* data, unsigned int
len)
to
  void SHA1Update(SHA1_CTX* context, unsigned char* data, unsigned
long len)

The 'len' parameter was declared an int which works fine on 32 bit machines.
However, on 16 bit machines an int is too small for the shifts being done
against
it.  This caused the hash function to generate incorrect values if len was
greater than 8191 (8K - 1) due to the 'len << 3' on line 3 of SHA1Update().

Since the file IO in main() reads 16K at a time, any file 8K or larger would
be guaranteed to generate the wrong hash (e.g. Test Vector #3, a million
"a"s).

I also changed the declaration of variables i & j in SHA1Update to
unsigned long from unsigned int for the same reason.

These changes should make no difference to any 32 bit implementations since
an
int and a long are the same size in those environments.

--
I also corrected a few compiler warnings generated by Borland C.
1. Added #include <process.h> for exit() prototype
2. Removed unused variable 'j' in SHA1Final
3. Changed exit(0) to return(0) at end of main.

ALL changes I made can be located by searching for comments containing 'JHB'
-----------------
Modified 8/98
By Steve Reid <sreid@sea-to-sky.net>
Still 100% public domain

1- Removed #include <process.h> and used return() instead of exit()
2- Fixed overwriting of finalcount in SHA1Final() (discovered by Chris Hall)
3- Changed email address from steve@edmweb.com to sreid@sea-to-sky.net

-----------------
Modified 4/01
By Saul Kravitz <Saul.Kravitz@celera.com>
Still 100% PD
Modified to run on Compaq Alpha hardware.

-----------------
Modified 07/2002
By Ralph Giles <giles@artofcode.com>
Still 100% public domain
modified for use with stdint types, autoconf
code cleanup, removed attribution comments
switched SHA1Final() argument order for consistency
use SHA1_ prefix for public api
move public api to sha1.h

-----------------
Modified 02/2018
By Ulrich Telle <github@telle-online.de>
Still 100% public domain
modified for use with fast-pbkdf2 (written by Joseph Birr-Pixton)
detect endianess at run-time
*/

/*
Test Vectors (from FIPS PUB 180-1)
"abc"
  A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
A million repetitions of "a"
  34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/

#include "mystdint.h"

#include <stdio.h>
#include <string.h>
#include "sha1.h"

#if 0
/* TODO: asm doesn't compile under Linux, use generic C equivalent for now */
#if __GNUC__ && (defined(__i386__) || defined(__x86_64__))
/*
* GCC by itself only generates left rotates.  Use right rotates if
* possible to be kinder to dinky implementations with iterative rotate
* instructions.
*/
#define SHA_ROT(op, x, k) \
        ({ unsigned int y; asm(op " %1,%0" : "=r" (y) : "I" (k), "0" (x)); y; })
#define rol(x,k) SHA_ROT("roll", x, k)
#define ror(x,k) SHA_ROT("rorl", x, k)
#else
/* Generic C equivalent */
#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#define ror(value, bits) (((value) << (32 - (bits))) | ((value) >> (bits)))
#endif
#endif

/* Generic C equivalent */
#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
#define ror(value, bits) (((value) << (32 - (bits))) | ((value) >> (bits)))

#define blk0le(i) (block[i] = (ror(block[i],8)&0xFF00FF00) \
                             |(rol(block[i],8)&0x00FF00FF))
#define blk0be(i) block[i]
#define blk(i) (block[i&15] = rol(block[(i+13)&15]^block[(i+8)&15] \
                             ^block[(i+2)&15]^block[i&15],1))

/*
* (R0+R1), R2, R3, R4 are the different operations (rounds) used in SHA1
*
* Rl0() for little-endian and Rb0() for big-endian.  Endianness is
* determined at run-time.
*/
#define Rl0(v,w,x,y,z,i) \
  z+=((w&(x^y))^y)+blk0le(i)+0x5A827999+rol(v,5);w=ror(w,2);
#define Rb0(v,w,x,y,z,i) \
  z+=((w&(x^y))^y)+blk0be(i)+0x5A827999+rol(v,5);w=ror(w,2);
#define R1(v,w,x,y,z,i) \
  z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=ror(w,2);
#define R2(v,w,x,y,z,i) \
  z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=ror(w,2);
#define R3(v,w,x,y,z,i) \
  z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=ror(w,2);
#define R4(v,w,x,y,z,i) \
  z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=ror(w,2);

/* Hash a single 512-bit block. This is the core of the algorithm. */
void sha1_transform(sha1_ctx *context, const uint8_t buffer[64])
{
  uint32_t a, b, c, d, e;
  static int one = 1;
  uint32_t block[16];
  memcpy(block, buffer, 64);

  /* Copy context->h[] to working vars */
  a = context->h[0];
  b = context->h[1];
  c = context->h[2];
  d = context->h[3];
  e = context->h[4];

  /* 4 rounds of 20 operations each. Loop unrolled. */
  if (1 == *(unsigned char*)&one) /* Check for endianess */
  {
    Rl0(a, b, c, d, e, 0);  Rl0(e, a, b, c, d, 1);  Rl0(d, e, a, b, c, 2);  Rl0(c, d, e, a, b, 3);
    Rl0(b, c, d, e, a, 4);  Rl0(a, b, c, d, e, 5);  Rl0(e, a, b, c, d, 6);  Rl0(d, e, a, b, c, 7);
    Rl0(c, d, e, a, b, 8);  Rl0(b, c, d, e, a, 9);  Rl0(a, b, c, d, e, 10); Rl0(e, a, b, c, d, 11);
    Rl0(d, e, a, b, c, 12); Rl0(c, d, e, a, b, 13); Rl0(b, c, d, e, a, 14); Rl0(a, b, c, d, e, 15);
  }
  else
  {
    Rb0(a, b, c, d, e, 0);  Rb0(e, a, b, c, d, 1);  Rb0(d, e, a, b, c, 2);  Rb0(c, d, e, a, b, 3);
    Rb0(b, c, d, e, a, 4);  Rb0(a, b, c, d, e, 5);  Rb0(e, a, b, c, d, 6);  Rb0(d, e, a, b, c, 7);
    Rb0(c, d, e, a, b, 8);  Rb0(b, c, d, e, a, 9);  Rb0(a, b, c, d, e, 10); Rb0(e, a, b, c, d, 11);
    Rb0(d, e, a, b, c, 12); Rb0(c, d, e, a, b, 13); Rb0(b, c, d, e, a, 14); Rb0(a, b, c, d, e, 15);
  }
  R1(e, a, b, c, d, 16); R1(d, e, a, b, c, 17); R1(c, d, e, a, b, 18); R1(b, c, d, e, a, 19);
  R2(a, b, c, d, e, 20); R2(e, a, b, c, d, 21); R2(d, e, a, b, c, 22); R2(c, d, e, a, b, 23);
  R2(b, c, d, e, a, 24); R2(a, b, c, d, e, 25); R2(e, a, b, c, d, 26); R2(d, e, a, b, c, 27);
  R2(c, d, e, a, b, 28); R2(b, c, d, e, a, 29); R2(a, b, c, d, e, 30); R2(e, a, b, c, d, 31);
  R2(d, e, a, b, c, 32); R2(c, d, e, a, b, 33); R2(b, c, d, e, a, 34); R2(a, b, c, d, e, 35);
  R2(e, a, b, c, d, 36); R2(d, e, a, b, c, 37); R2(c, d, e, a, b, 38); R2(b, c, d, e, a, 39);
  R3(a, b, c, d, e, 40); R3(e, a, b, c, d, 41); R3(d, e, a, b, c, 42); R3(c, d, e, a, b, 43);
  R3(b, c, d, e, a, 44); R3(a, b, c, d, e, 45); R3(e, a, b, c, d, 46); R3(d, e, a, b, c, 47);
  R3(c, d, e, a, b, 48); R3(b, c, d, e, a, 49); R3(a, b, c, d, e, 50); R3(e, a, b, c, d, 51);
  R3(d, e, a, b, c, 52); R3(c, d, e, a, b, 53); R3(b, c, d, e, a, 54); R3(a, b, c, d, e, 55);
  R3(e, a, b, c, d, 56); R3(d, e, a, b, c, 57); R3(c, d, e, a, b, 58); R3(b, c, d, e, a, 59);
  R4(a, b, c, d, e, 60); R4(e, a, b, c, d, 61); R4(d, e, a, b, c, 62); R4(c, d, e, a, b, 63);
  R4(b, c, d, e, a, 64); R4(a, b, c, d, e, 65); R4(e, a, b, c, d, 66); R4(d, e, a, b, c, 67);
  R4(c, d, e, a, b, 68); R4(b, c, d, e, a, 69); R4(a, b, c, d, e, 70); R4(e, a, b, c, d, 71);
  R4(d, e, a, b, c, 72); R4(c, d, e, a, b, 73); R4(b, c, d, e, a, 74); R4(a, b, c, d, e, 75);
  R4(e, a, b, c, d, 76); R4(d, e, a, b, c, 77); R4(c, d, e, a, b, 78); R4(b, c, d, e, a, 79);

  /* Add the working vars back into context.state[] */
  context->h[0] += a;
  context->h[1] += b;
  context->h[2] += c;
  context->h[3] += d;
  context->h[4] += e;

  /* Wipe variables */
  a = b = c = d = e = 0;
  memset(block, 0, 64);
}


/**
* Initialize new context
*
* @param context SHA1-Context
*/
void sha1_init(sha1_ctx *context)
{
  /* SHA1 initialization constants */
  context->h[0] = 0x67452301;
  context->h[1] = 0xefcdab89;
  context->h[2] = 0x98badcfe;
  context->h[3] = 0x10325476;
  context->h[4] = 0xc3d2e1f0;
  context->count[0] = context->count[1] = 0;
}


/**
* Run your data through this
*
* @param context SHA1-Context
* @param p       Buffer to run SHA1 on
* @param len     Number of bytes
*/
void sha1_update(sha1_ctx *context, const void *p, size_t len)
{
  const uint8_t *data = p;
  size_t i, j;

  j = (context->count[0] >> 3) & 63;
  if ((context->count[0] += (uint32_t) (len << 3)) < (len << 3))
  {
    context->count[1]++;
  }
  context->count[1] += (uint32_t) (len >> 29);
  if ((j + len) > 63)
  {
    memcpy(&context->buffer[j], data, (i = 64 - j));
    sha1_transform(context, context->buffer);
    for (; i + 63 < len; i += 64)
    {
      sha1_transform(context, data + i);
    }
    j = 0;
  }
  else
  {
    i = 0;
  }
  memcpy(&context->buffer[j], &data[i], len - i);
}


/**
* Add padding and return the message digest
*
* @param digest  Generated message digest
* @param context SHA1-Context
*/
void sha1_final(sha1_ctx *context, uint8_t digest[SHA1_DIGEST_SIZE])
{
  uint32_t i;
  uint8_t finalcount[8];

  for (i = 0; i < 8; i++)
  {
    finalcount[i] = (uint8_t) ((context->count[(i >= 4 ? 0 : 1)]
                    >> ((3 - (i & 3)) * 8)) & 255);
  }
  sha1_update(context, (uint8_t *) "\200", 1);
  while ((context->count[0] & 504) != 448)
  {
    sha1_update(context, (uint8_t *) "\0", 1);
  }
  sha1_update(context, finalcount, 8); /* Should cause SHA1_Transform */
  for (i = 0; i < SHA1_DIGEST_SIZE; i++)
  {
    digest[i] = (uint8_t)
                ((context->h[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
  }

  /* Wipe variables */
  i = 0;
  memset(context->buffer, 0, 64);
  /* fast-pbkdf2 needs access to the state*/
  /*memset(context->h, 0, 20);*/
  memset(context->count, 0, 8);
  memset(finalcount, 0, 8);    /* SWR */
}
