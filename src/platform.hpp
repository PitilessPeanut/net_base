#ifndef PLATFORM_HPP
#define PLATFORM_HPP

#include <cstdint>
#include <type_traits>

typedef std::uint_fast64_t u64;
typedef std::uint_fast32_t u32;
typedef std::uint_fast16_t u16;
typedef std::uint_fast8_t  u8;
typedef std::int_fast64_t  i64;
typedef std::int_fast32_t  i32;
typedef std::int_fast16_t  i16;
typedef std::int_fast8_t   i8;


#ifndef BOOST_FORCEINLINE
  #define BOOST_FORCEINLINE inline
#endif
#ifndef BOOST_CXX14_CONSTEXPR
  #define BOOST_CXX14_CONSTEXPR constexpr
#endif
// xcode: User header search path
// vs: properties->VC++ Directories->Include Directories
#include "../deps/boost/ignore_unused.hpp"


/****************************************/
/*         Protected or removed strings */
/****************************************/
    #if DO_PROTECT_STRINGS
      #if DO_REMOVE_STRINGS
        #define DBGSTR(str) " "
      #else
        #define DBGSTR(str) str
      #endif
      #define PROTECTED(str) str // todo
    #else
      #define REMOVE(str) str // todo protect
      #define PROTECTED(str) str
    #endif


/****************************************/
/*                    Missing keywords! */
/****************************************/
    #define implicit
    #define FALLTHROUGH    [[fallthrough]];


/****************************************/
/*                            Consteval */
/****************************************/
    [[nodiscard]] constexpr bool is_constant_evaluated() noexcept
    {
        return std::is_constant_evaluated(); //__builtin_is_constant_evaluated();
    }


/****************************************/
/*                           PCG Random */
/****************************************/
    class pcg64
    {
    private:
        static constexpr auto PCG_DEFAULT_MULTIPLIER = 6364136223846793005ull;

        u64 state, inc;
        
    public:
        explicit constexpr pcg64(const u64 seed = 0);
        
        constexpr u32 operator()();
    };

    u32 pcg64Rand(const u64 seed=0);


/****************************************/
/*                               Random */
/****************************************/
    unsigned getRand(void *pEverything);


/****************************************/
/*                                Debug */
/****************************************/
    void debugPrint(const char *debugStr, void *pDevice);
    
    class Debug
    {
    public:
        enum class Level { trace, info, attention, warning, error };
    
    private:
        static constexpr int SZ_BUF = 240;
        
        struct Buf
        {
            char buf[SZ_BUF] = {0};
        };
        
        void append(Buf *res, Buf *prev, const Debug::Level level) const;
        
        void append(Buf *res, Buf *prev, const bool val) const;
        
        void append(Buf *res, Buf *prev, const char val) const;
        
        void append(Buf *res, Buf *prev, const int val) const;

        void append(Buf *res, Buf *prev, const long val) const;
        
        void append(Buf *res, Buf *prev, const unsigned val) const;

        void append(Buf *res, Buf *prev, const unsigned long val) const;
        
        void append(Buf *res, Buf *prev, const float val) const;
        
        void append(Buf *res, Buf *prev, const char *str) const;
        
        void printDone(const char *res) const;
        
        template <typename Last>
        void internal_print(Buf *res, Buf *prev, Last last)
        {
            append(res, prev, last);
            printDone(res->buf);
        }
        
        template <typename Current, typename... Rest>
        void internal_print(Buf *res, Buf *prev, Current str, Rest... rest)
        {
            append(res, prev, str);
            Buf *temp = res;
            res = prev;
            prev = temp;
            internal_print(res, prev, rest...);
        }
        
    public:
        template <typename... Str>
        static void print(Str... str)
        {
            static Debug debug;
            Buf freshBufA;
            Buf freshBufB;
            Buf *res=&freshBufA, *prev=&freshBufB;
            debug.internal_print(res, prev, str...);
        }
    };


#else
  #error "double include"
#endif // PLATFORM_HPP

