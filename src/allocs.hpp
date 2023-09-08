#ifndef ALLOCS_HPP
#define ALLOCS_HPP


/****************************************/
/*                               Config */
/****************************************/


/****************************************/
/*                           Global mem */
/****************************************/
    class Alloc
    {
    private:
        unsigned char *budget;
        
        int used, totaAvail;
        
    public:
        Alloc() {}
        
        Alloc(const Alloc&) = delete;
        Alloc& operator=(const Alloc&) = delete;
        
        constexpr void setup(void *rawMem, const unsigned availMem);
        
        constexpr unsigned char *alloc(const int amount);
        
        constexpr void expensiveFree();
    };

    Alloc& getAlloc();

    void setupAlloc(void *rawMem, const unsigned availMem);

    void *alloc(const int amount);


#else
  #error "double include"
#endif // ALLOCS_HPP
