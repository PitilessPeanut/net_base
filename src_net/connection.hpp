#ifndef CONNECTION_HPP
#define CONNECTION_HPP


/*****************************************************************************/
/* Connection pool                                                           */ 
/*****************************************************************************/
    class Pool
    {          
    public:
        enum class Method {GET,POST,PUT,DLETE,HEAD};

        enum class Format {TEXT,JSON};
        
        static constexpr int max_concurrent_connections = 1;
        static constexpr int max_concurrent_connections_ssl = 5;
        
    private:
        struct PoolMembers;
        PoolMembers *pPoolMembers;

        template <bool SSL, bool ConnectSocks, typename Connection>
        void request_internal( Connection& connection
                             , const int callerId
                             , const char *userPwHost
                             , int userPwHostLen
                             , const char *url
                             , int urllen
                             , const char *data
                             , int datalen
                             , const char *auth
                             , int authlen
                             , const char *xApiKey
                             , int xApiKeylen
                             , const Method method
                             , const Format formatJson
                             , const unsigned short socks5port = 0
                             );
        
    public:
        explicit Pool(void *global);

        Pool(const Pool&) = delete;
        Pool& operator=(const Pool&) = delete;

        void request( const int callerId
                    , const char *userPwHost
                    , int userPwHostLen
                    , const char *url
                    , int urllen
                    , const char *data
                    , int datalen
                    , const char *auth
                    , int authlen
                    , const char *xApiKey
                    , int xApiKeylen
                    , const Method method
                    , const Format format
                    );

        void requestSSL( const int callerId
                       , const char *userPwHost
                       , int userPwHostLen
                       , const char *url
                       , int urllen
                       , const char *data
                       , int datalen
                       , const char *auth
                       , int authlen
                       , const char *xApiKey
                       , int xApiKeylen
                       , const Method method
                       , const Format format
                       );

        void requestSocks5( const int callerId
                          , const char *userPwHost
                          , int userPwHostLen
                          , const char *url
                          , int urllen
                          , const char *data
                          , int datalen
                          , const char *auth
                          , int authlen
                          , const char *xApiKey
                          , int xApiKeylen
                          , const Method method
                          , const Format format
                          , const unsigned short socks5port
                          );

        void requestSocks5SSL( const int callerId
                             , const char *userPwHost
                             , int userPwHostLen
                             , const char *url
                             , int urllen
                             , const char *data
                             , int datalen
                             , const char *auth
                             , int authlen
                             , const char *xApiKey
                             , int xApiKeylen
                             , const Method method
                             , const Format format
                             , const unsigned short socks5port
                             );

        void getReply(const int callerId, void *dst, int& statusCode);
        
        ~Pool();
    };

    
#else
  #error "double include"
#endif // CONNECTION_HPP
