#include "platform.hpp"
#include <stdio.h>


/****************************************/
/*                           PCG Random */
/****************************************/
    constexpr pcg64::pcg64(const u64 seed)
      : state(0), inc(0)
    {
        const u64 initstate = seed << 31 | seed;
        const u64 initseq = seed << 31 | seed;
        inc = (initseq << 1) | 1;
        state = initstate + inc;
        state = state * PCG_DEFAULT_MULTIPLIER + inc;
    }

    constexpr u32 pcg64::operator()()
    {
        // PCG Random number generation (http://www.pcg-random.org):
        const u64 oldstate = state;
        // Advance internal state:
        state = oldstate * PCG_DEFAULT_MULTIPLIER + (inc | 1);
        // Calculate output function (XSH RR), uses old state for max ILP:
        const u32 xorshifted = u32(((oldstate >> 18u) ^ oldstate) >> 27u);
        const u32 rot = oldstate >> 59u;
        return (xorshifted >> rot) | (xorshifted << ((~rot) & 31));
    }

    u32 pcg64Rand(const u64 seed)
    {
        static pcg64 genrand(seed);
        return genrand();
    }


/****************************************/
/*                                Debug */
/****************************************/
    void Debug::append(Buf *res, Buf *prev, const Debug::Level level) const
    {
        auto printLevel = [level]
                          {
                              switch (level)
                              {
                                  // '\e' is non-ISO standard
                                  case     Level::trace: return DBGSTR("[Trace]     ");
                                  case      Level::info: return DBGSTR("[\033[1;34mInfo\033[0m]      ");
                                  case Level::attention: return DBGSTR("[\033[1;32mAttention\033[0m] ");
                                  case   Level::warning: return DBGSTR("[\033[1;33mWarning\033[0m]   ");
                                  case     Level::error: return DBGSTR("[\033[1;31mError\033[0m]     ");
                                  default              : return " ";
                              }
                          };
        const int r = snprintf(res->buf, SZ_BUF, "%s%s", prev->buf, printLevel());
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }
    
    void Debug::append(Buf *res, Buf *prev, const bool val) const
    {
        const int r = snprintf(res->buf, SZ_BUF, "%s%s", prev->buf, val==true ? "true" : "false");
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }
     
    void Debug::append(Buf *res, Buf *prev, const char val) const
    {
        const int r = snprintf(res->buf, SZ_BUF, "%s%c", prev->buf, val);
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }

    void Debug::append(Buf *res, Buf *prev, const int val) const
    {
        const int r = snprintf(res->buf, SZ_BUF, "%s%d", prev->buf, val);
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }

    void Debug::append(Buf *res, Buf *prev, const long val) const
    {
        const int r = snprintf(res->buf, SZ_BUF, "%s%ld", prev->buf, val);
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }

    void Debug::append(Buf *res, Buf *prev, const unsigned val) const
    {
        const int r = snprintf(res->buf, SZ_BUF, "%s%u", prev->buf, val);
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }

    void Debug::append(Buf *res, Buf *prev, const unsigned long val) const
    {
        const int r = snprintf(res->buf, SZ_BUF, "%s%lu", prev->buf, val);
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }

    void Debug::append(Buf *res, Buf *prev, const float val) const
    {
        const int r = snprintf(res->buf, SZ_BUF, "%s%f", prev->buf, val);
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }

    void Debug::append(Buf *res, Buf *prev, const char *str) const
    {
        const int r = snprintf(res->buf, SZ_BUF, "%s%s", prev->buf, str);
        if (r < 0)
            snprintf(res->buf, 12, DBGSTR("truncation\n"));
    }

    void Debug::printDone(const char *res) const
    {
        debugPrint(res, nullptr);
    }
    
