#include "encode.hpp"
#include <immintrin.h>


/****************************************/
/*                            meow_hash */
/*                                      */
/* Simplified version of                */
/* github.com/cmuratori/meow_hash       */
/* (zlib License!)                      */
/****************************************/
    enc_u64 simplehash_x86_64(const char *str, enc_u32 len)
    {
        static const unsigned char restMask[32] =
            { 255,255,255,255, 255,255,255,255, 255,255,255,255, 255,255,255,255,
                0,  0,  0,  0,   0,  0,  0,  0,   0,  0,  0,  0,   0,  0,  0,  0
            };
            
        static const unsigned char defaultSeed[16] =
            { 178, 201, 95, 240, 40, 41, 143, 216,
              2, 209, 178, 114, 232, 4, 176, 188
            };
            
        __m128i hashValue = _mm_cvtsi64_si128(len);
        hashValue = _mm_xor_si128(hashValue, _mm_loadu_si128((const __m128i *)defaultSeed));
    
        size_t chunkCount = len / 16;
        while (chunkCount--)
        {
            __m128i in = _mm_loadu_si128((const __m128i *)str);
            str += 16;
    
            hashValue = _mm_xor_si128(hashValue, in);
            hashValue =_mm_aesdec_si128(hashValue, _mm_setzero_si128());
        }
    
        size_t rest = len % 16;
    
        char temp[16] = {0};
        const void *dst = __builtin_memcpy((unsigned char *)temp, str, rest);
        (void)dst;
        __m128i in = _mm_loadu_si128((__m128i *)temp);
        in = _mm_and_si128(in, _mm_loadu_si128((const __m128i *)(restMask + 16 - rest)));
        hashValue = _mm_xor_si128(hashValue, in);
        hashValue = _mm_aesdec_si128(hashValue, _mm_setzero_si128());
        
        return _mm_extract_epi64(hashValue, 0) ^ _mm_extract_epi64(hashValue, 1);
    }


/****************************************/
/*                        base64 encode */
/****************************************/ 
    constexpr void base64encode_impl(char *dst, const char *src, int szSrc)
    {
        constexpr char base64alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
        constexpr char pad = '=';
    
        int dstIdx=0, i=0;
        for (; (i+2) < szSrc; i+=3)
        {
            dst[dstIdx++] = base64alphabet[ (src[i]>>2) & 0x3f];                               
            dst[dstIdx++] = base64alphabet[((src[i]  &0x03) << 4) | ((src[i+1] & 0xf0) >> 4)];
            dst[dstIdx++] = base64alphabet[((src[i+1]&0x0f) << 2) | ((src[i+2] & 0xc0) >> 6)];
            dst[dstIdx++] = base64alphabet[  src[i+2]&0x3f];
        }
        if (i < szSrc)
        {
            dst[dstIdx++] = base64alphabet[(src[i]>>2) & 0x3f];
            if (i+1 == szSrc)
            {
                dst[dstIdx++] = base64alphabet[(src[i]&0x03) << 4]; 
                dst[dstIdx++] = pad;                                
            }
            else
            {
                dst[dstIdx++] = base64alphabet[((src[i]  &0x03) << 4) | ((src[i+1] & 0xf0) >> 4)];
                dst[dstIdx++] = base64alphabet[ (src[i+1]&0x0f) << 2];                            
            }
            dst[dstIdx] = pad;
        }
    }

    void base64encode(char *dst, const char *src, int szSrc)
    {
        base64encode_impl(dst, src, szSrc);
    }


/****************************************/
/*                        base64 decode */
/****************************************/ 
    constexpr void base64decode_impl(char *dst, const char *src, int szSrc)
    {
        constexpr char pad = '=';
        constexpr struct T
        {
            int vals[256];
            constexpr T() : vals()
            {
                for (int c=0, i=0; i<256; ++i)
                {
                    // Table using RLE:
                    vals[i] = ":y:zopqrstuvwx:9:;<=>?@ABCDEFGHIJKLMNOPQRST:UVWXYZ[\\]^_`abcdefghijklmn:"[c]-';';
                    c += "*+./0123456789<=@ABCDEFGHIJKLMNOPQRSTUVWXYZ`abcdefghijklmnopqrstuvwxyz\0"[c] == i;
                }
            }
            constexpr int operator[](int pos) const
            {
                return vals[pos];
            }
        } bitTable;
    
        if (szSrc == 0)
            return;
        if (szSrc%4 != 0)
            return;
        int dstLen = (szSrc/4)*3;
        if (src[szSrc-1] == pad)
        {
            dstLen -= 1;
            if (src[szSrc-2] == pad)
                dstLen -= 1;
        }
    
        int srcBufLen = szSrc;
        while (srcBufLen>0 && src[srcBufLen-1]==pad)
            srcBufLen -= 1;
        bool isValid = true;
        for (int i=0; i < srcBufLen; ++i)
        {
            const char c = src[i];
            isValid = isValid && ((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='/' && c<='9') || (c == '+'));
        }
        if (!isValid)
            return;
        
        int dstIdx = 0;
        int srcIdx = 0;
        while (srcBufLen > 4)
        {
            const int a = src[srcIdx+0], b = src[srcIdx+1], c = src[srcIdx+2], d = src[srcIdx+3];
            dst[dstIdx] = (bitTable[a] << 2 | bitTable[b] >> 4);  dstIdx += 1;
            dst[dstIdx] = (bitTable[b] << 4 | bitTable[c] >> 2);  dstIdx += 1;
            dst[dstIdx] = (bitTable[c] << 6 | bitTable[d]);       dstIdx += 1;
            srcIdx += 4;
            srcBufLen -= 4;
        }
        const int a = src[srcIdx+0], b = src[srcIdx+1], c = src[srcIdx+2], d = src[srcIdx+3];
        if (srcBufLen > 1)
        {
            dst[dstIdx] = (bitTable[a] << 2 | bitTable[b] >> 4);
            dstIdx += 1;
        }
        if (srcBufLen > 2)
        {
            dst[dstIdx] = (bitTable[b] << 4 | bitTable[c] >> 2);
            dstIdx += 1;
        }
        if (srcBufLen > 3)
        {
            dst[dstIdx] = (bitTable[c] << 6 | bitTable[d]);
        }
    }

    void base64decode(char *dst, const char *src, int szSrc)
    {
        base64decode_impl(dst, src, szSrc);
    }
    

    static_assert([]
                  {
                      const char test[14] = "login:passwor";
                      const size_t srcSizeWithoutZero = sizeof(test) - 1;
                      constexpr auto szDst = base64encode_getRequiredSize(srcSizeWithoutZero);
                      char dst[szDst] = {0};
                      base64encode_impl(dst, test, srcSizeWithoutZero);
                      bool ok = true;
                      for (int i=0; i<szDst; ++i)
                          ok = ok && (dst[i]=="bG9naW46cGFzc3dvcg=="[i]);
                      return (szDst==20) && ok;
                  }()
                 );

    static_assert([]
                  {
                      const char test[21] = "bG9naW46cGFzc3dvcg==";
                      const size_t srcSizeWithoutZero = sizeof(test) - 1;
                      constexpr auto szDst = base64decode_getRequiredSize(srcSizeWithoutZero);
                      char dst[szDst] = {0};
                      //todo
                      return true;
                  }()
                 );
                 
