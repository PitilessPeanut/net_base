#ifndef ASMTYPES_HPP
#define ASMTYPES_HPP

// 32 bit:
typedef decltype([]
                 {
                     constexpr auto u64a = 0ull;
                     constexpr auto u32a = 0ul;
                     constexpr auto u32b = 0u;
                     if constexpr (sizeof(u32a) == (sizeof(u64a) / 2))
                         return u32a;
                     else if constexpr (sizeof(u32b) == (sizeof(u64a) / 2))
                         return u32b;
                 }()
                )
        UDWORD;
typedef decltype([]
                 {
                     constexpr auto i64a = 0ll;
                     constexpr auto i32a = 0l;
                     constexpr auto i32b = 0;
                     if constexpr (sizeof(i32a) == (sizeof(i64a) / 2))
                         return i32a;
                     else if constexpr (sizeof(i32b) == (sizeof(i64a) / 2))
                         return i32b;
                 }()
                )
        SDWORD;
static_assert(sizeof(UDWORD) == sizeof(SDWORD));
static_assert([]{ constexpr UDWORD x = ~0; return x >> 31; }());
typedef UDWORD ULONG;
typedef SDWORD SLONG;

// 64 bit:
typedef decltype([]{ constexpr auto u64 = 0ull; return u64; }()) UQWORD;
typedef decltype([]{ constexpr auto i64 = 0ll ; return i64; }()) SQWORD;
static_assert(sizeof(UQWORD) == sizeof(SQWORD));
static_assert([] { constexpr UQWORD x = ~0; return x>>63; }());
static_assert(sizeof(UQWORD) == (sizeof(UDWORD)<<1));

// 16 bit:
typedef decltype([]{ constexpr unsigned short u16 = 0; return u16; }()) UWORD;
typedef decltype([]{ constexpr signed short i16 = 0; return i16; }()) SWORD;
static_assert(sizeof(UWORD) == sizeof(SWORD));
static_assert([]{ constexpr UWORD x = ~0; return x>>15; }());
static_assert(sizeof(UDWORD) == (sizeof(UWORD)<<1));

// 8 bit:
constexpr unsigned CHARBITS = []{ return 32u / sizeof(UDWORD); }();
typedef unsigned char UBYTE;
typedef signed char SBYTE;
static_assert((sizeof(UBYTE)*CHARBITS) == 8);
static_assert(sizeof(UBYTE) == sizeof(SBYTE));




#endif // ASMTYPES_HPP

