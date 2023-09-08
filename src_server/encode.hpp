#ifndef ENCODE_HPP
#define ENCODE_HPP

/****************************************/
/*                            Datatypes */
/****************************************/
    typedef unsigned long long enc_u64;
    typedef unsigned           enc_u32;
    typedef unsigned char      enc_u8;
    constexpr unsigned enc_charbits = []{ return 32u / sizeof(enc_u32); }();
    static_assert([]{ constexpr enc_u64 x = ~0ull; return x>>63; }());
    static_assert([]{ constexpr enc_u32 x = ~0u; return x>>31; }());
    static_assert(sizeof(enc_u64)*enc_charbits == 64);
    static_assert(sizeof(enc_u8)*enc_charbits == 8);
    

/****************************************/
/*                                extra */
/****************************************/
    #define FALLTHROUGH    [[fallthrough]];


/****************************************/
/*                 simplyfied meow_hash */
/****************************************/
    enc_u64 simplehash_x86_64(const char *str, enc_u32 len);


/****************************************/
/*                                fnv64 */
/****************************************/
    template <typename Int>
    constexpr enc_u64 FNV1a(const char *str, const Int len_)
    {
        const unsigned len = (unsigned)len_;
        enc_u64 h = 14695981039346656037ull;
        constexpr enc_u64 FNV_prime = 1099511628211ull;
        for (unsigned i=0; i < (len & ~7); i += 8)
        {
            h = h ^ ((enc_u64(str[i+7]) << 56) +
                     (enc_u64(str[i+6]) << 48) +
                     (enc_u64(str[i+5]) << 40) +
                     (enc_u64(str[i+4]) << 32) +
                     (enc_u64(str[i+3]) << 24) +
                     (enc_u64(str[i+2]) << 16) +
                     (enc_u64(str[i+1]) <<  8) +
                      enc_u64(str[i+0])
                    );
            h = h * FNV_prime;
        }
        switch (len - (len & ~7))
        {
            case 7 : h = h ^ enc_u64(str[len-7]) << 48; FALLTHROUGH
            case 6 : h = h ^ enc_u64(str[len-6]) << 40; FALLTHROUGH
            case 5 : h = h ^ enc_u64(str[len-5]) << 32; FALLTHROUGH
            case 4 : h = h ^ enc_u64(str[len-4]) << 24; FALLTHROUGH
            case 3 : h = h ^ enc_u64(str[len-3]) << 16; FALLTHROUGH
            case 2 : h = h ^ enc_u64(str[len-2]) <<  8; FALLTHROUGH
            case 1 : h = h ^ enc_u64(str[len-1]);
        }
        return h * FNV_prime;
    }


/****************************************/
/*                        base64 encode */
/****************************************/
    template <typename Int>
    constexpr Int base64encode_getRequiredSize(Int szSrc)
    {
        return ((szSrc+2)/3)*4;
    }

    void base64encode(char *dst, const char *src, int szSrc);
    
    template <typename Int>
    void base64encode(char *dst, const char *src, Int szSrc)
    {
        base64encode(dst, src, (int)szSrc);
    }


/****************************************/
/*                        base64 decode */
/****************************************/ 
    template <typename Int>
    constexpr Int base64decode_getRequiredSize(Int szSrc)
    {
        return (szSrc/4)*3;
    }

    void base64decode(char *dst, const char *src, int szSrc);

    template <typename Int>
    void base64decode(char *dst, const char *src, Int szSrc)
    {
        base64decode(dst, src, (int)szSrc);
    }

    
#else
  #error "include err"
#endif // ENCODE_HPP

