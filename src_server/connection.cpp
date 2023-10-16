#include "connection.hpp"
#include "global.hpp"
#include "../src/platform.hpp"
#include "encode.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/lexical_cast.hpp>
#include <vector>
#include <string_view>
#include <optional>
#include <unordered_map>

using std::vector;
using std::string_view;
using std::string;
using std::unique_ptr;
using std::optional;
using std::lock_guard;
using std::mutex;
using std::unordered_map;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;
using enum Debug::Level;

#define NON_CONST


/*****************************************************************************/
/* Tcp Connection                                                            */ 
/*****************************************************************************/ 
    /****************************************/
    /*       Internal container for a reply */
    /****************************************/
    struct Reply
    {
        int       http_status;
        string    reply;
    };

    
    /****************************************/
    /*                            getSock() */
    /****************************************/
    inline auto& getSock(beast::tcp_stream& sock)
    {
        return sock;
    }
    
    inline auto& getSock(beast::ssl_stream<beast::tcp_stream>& sock)
    {
        return beast::get_lowest_layer(sock);
    }
            
    
    /****************************************/
    /*                              TLS/SSL */
    /****************************************/
    struct SSL_on
    {
        boost::asio::ssl::context ssl_ctx;
        
        static constexpr bool DoSSL = true;
        
        SSL_on()
          : ssl_ctx(boost::asio::ssl::context::tlsv12_client)
        {}
    };
        
    
    struct SSL_off
    {
        static constexpr bool DoSSL = false;
    };


    /****************************************/
    /*                Single Tcp Connection */
    /****************************************/
    template <class Sockettype, class SSL>
    struct TcpConnection : public SSL
    {  
        unique_ptr<AtomicFlag> busy;

        unique_ptr<std::atomic<u64>> hostHash;

        unique_ptr<Sockettype> stream;

        beast::flat_buffer buffer; // Must persist between reads

        string host, userpw;

        asio::ip::tcp::resolver resolver;

        boost::beast::http::request<boost::beast::http::string_body> req;
        // Must be 'optional' to avoid 'double body' problem:
        optional<boost::beast::http::response<boost::beast::http::string_body>> res; 

        int callerId;
        
        int totalread = 0, totalConsecutiveReads = 0;
    
        boost::asio::deadline_timer keepaliveTimer;
    
        mutex&                        replies_lock;
        unordered_map<int, Reply>&    replies;
    
        auto makeSock(asio::io_context& ioc) NON_CONST
        {
            if constexpr (this->DoSSL)
                return make_unique<beast::ssl_stream<beast::tcp_stream>>(make_strand(ioc), this->ssl_ctx);
            else
                return make_unique<beast::tcp_stream>(make_strand(ioc));
        }

        void setupHttpRequest( auto& req_
                             , const Pool::Method method
                             , string_view url
                             , string_view data
                             , string_view auth
                             , string_view xApiKey
                             , const Pool::Format format
                             ) const
        {
            req_.version(11); // http 1.1
            
            using enum Pool::Method;
            switch (method)
            {
                default     :
                case    GET : { req_.method(beast::http::verb::get); }
                break;
                case   POST : { req_.method(beast::http::verb::post);
                                req_.set( beast::http::field::content_length
                                        , boost::lexical_cast<std::string>(data.size())
                                        );
                              }
                break;
                case    PUT : { req_.method(beast::http::verb::put); }
                break;
                case DELETE : { req_.method(beast::http::verb::delete_); }
                break;
                case   HEAD : { req_.method(beast::http::verb::head); }
                break;
            }

            //string target(userpw); todo: test/remove
            //target += url;

            req_.target(beast::string_view(url.data(), url.size()));
            //req_.target(target);
            req_.set(beast::http::field::host, host);
            req_.set(beast::http::field::connection, PROTECTED("Keep-Alive"));
            if (userpw.empty() == false)
            {
                string login(base64encode_getRequiredSize(userpw.size()), '\0');
                base64encode(login.data(), userpw.c_str(), userpw.size());
                req_.set(beast::http::field::authorization, PROTECTED("Basic ")+login); // login:pass => base64
            }
            else if (auth.empty() == false)
            {
                req_.set(http::field::authorization, auth);
            }
            if (format == Pool::Format::JSON)
                req_.set(PROTECTED("Content-Type"), PROTECTED("application/json"));
            if (data.empty() == false)
            {
                req_.body() = data;
                req_.prepare_payload(); // Adjust lenght and encoding based on body
            }
            if (xApiKey.empty() == false)
                req_.set(PROTECTED("X-Api-Key"), xApiKey);
        }
    
        TcpConnection(asio::io_context& ioc, mutex& mtx, unordered_map<int, Reply>& r)
          : busy(std::make_unique<AtomicFlag>())
          , hostHash(std::make_unique<std::atomic<u64>>(0))
          , stream(makeSock(ioc))
          , resolver(make_strand(ioc))
          , keepaliveTimer(ioc)
          , replies_lock(mtx)
          , replies(r)
        {
            busy->makeAvail();
        }
        
        template <bool ConnectSocks5>
        void establish( const int caller
                      , string_view newhost
                      , string_view url
                      , string_view data
                      , string_view auth
                      , string_view xApiKey
                      , const Pool::Method method
                      , const Pool::Format format
                      , const unsigned short socks5port)
        {
            // We remain busy for the whole operation:
            const AtomicFlag::State isBusy = busy->isAvail_then_lock();
            boost::ignore_unused(isBusy);
        
            callerId = caller;

            if ( const auto userpwPos = newhost.find("@")
               ; userpwPos>0 && userpwPos<newhost.size()
               )
            {
                userpw = newhost.substr(0, userpwPos);
                host = newhost.substr(userpwPos+1);
            }
            else
            {
                host = newhost;
            }

            hostHash->store(simplehash(host.data(), (int)host.size()), std::memory_order_relaxed);

            setupHttpRequest(req, method, url, data, auth, xApiKey, format);
        
            string port;
            if ( const auto dblePoint = host.rfind(":")
               ; dblePoint>0 && dblePoint<host.size()
               )
            {
                port = host.substr(dblePoint+1);
                host = host.substr(0, dblePoint);
            }

            if (port.empty())
            {
                if constexpr (this->DoSSL)
                    port = PROTECTED("443");
                else
                    port = PROTECTED("80");
            }

            auto connect = [this](beast::error_code ec, asio::ip::tcp::resolver::results_type results)
                           {
                               if (ec)
                               {
                                   hostHash->store(0, std::memory_order_relaxed);
                                   return Debug::print(Debug::Level::warning, DBGSTR("[]connect: "), ec.message().c_str(), DBGSTR(", host: "), host.c_str());
                               }
    
                               // "10 seconds seems excessive for most sites" news.ycombinator.com/item?it=25832283
                               getSock(*this->stream).expires_after(std::chrono::seconds(9));

                               if constexpr (this->DoSSL)
                                   getSock(*this->stream).async_connect(results, beast::bind_front_handler(&TcpConnection::handshake, this));
                               else
                                   getSock(*this->stream).async_connect(results, beast::bind_front_handler(&TcpConnection::write, this));
                           };

            if constexpr (ConnectSocks5)
            {
                // more detailed impl.: github.com/sehe/asio-socks45-client/blob/main/socks5.hpp
                // stackoverflow.com/questions/69778112/is-there-a-native-support-for-proxy-connection-via-socks5-for-boostasio
                // develop.socks-proto.cpp.al/socks/quick_look/async_client0.html
                try
                {
                    getSock(*stream).connect(asio::ip::tcp::endpoint({}, socks5port));
    
                    constexpr char greeting1[] = { 0x05 // socks5
                                                 , 0x01 // One authentication method
                                                 , 0x00 // No authentication
                                                 };
                    asio::write(getSock(*stream), asio::buffer(greeting1, sizeof(greeting1)));
                    char resp1[2] = {0};
                    asio::read(getSock(*stream), asio::buffer(resp1,2), asio::transfer_exactly(2));
                    if (resp1[1] != 0x00)
                        return Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::establish(): socks5 auth error "));
                    vector<unsigned char> greeting2 = { 0x05 // socks 5
                                                      , 0x01 // connect
                                                      , 0x00 // reserved
                                                      , 0x03 // "domain"
                                                      , (unsigned char)(host.size() & 255) // hostname len
                                                      };
                    std::copy(host.begin(), host.end(), back_inserter(greeting2));            
                    const unsigned short dstPort = (unsigned short)stoi(port) & 0xffff;
                    const unsigned char msb = (unsigned char)((dstPort>>8) & 255),
                                        lsb = (unsigned char)(dstPort & 255);
                    greeting2.push_back( msb );
                    greeting2.push_back( lsb );
                    asio::write(getSock(*stream), asio::buffer(greeting2));
                    char resp2[10] = {0};
                    asio::read(getSock(*stream), asio::buffer(resp2, sizeof(resp2)), asio::transfer_exactly(sizeof(resp2)));
                    if (resp2[1] != 0)
                        return Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::establish(): socks5 error: "), (int)resp2[1]);
                }
                catch (...)
                {
                    return Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::establish(): Socks connect failed"));
                }
            
                if constexpr (this->DoSSL)
                {
                    stream->async_handshake( asio::ssl::stream_base::handshake_type::client
                                           , beast::bind_front_handler(&TcpConnection::writeSSL, this)
                                           );
                }
                else
                {
                    beast::http::async_write(*stream, req, beast::bind_front_handler(&TcpConnection::read, this));   
                }
            }
            else
            {
                resolver.async_resolve(host, port, connect);
            }
    
            boost::ignore_unused(connect, socks5port);
        }

        void handshake(beast::error_code ec, asio::ip::tcp::resolver::endpoint_type)
        {
            if (ec)
                return Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::handshake(): "), ec.message().c_str());
            
            if constexpr (this->DoSSL)
                stream->async_handshake( asio::ssl::stream_base::handshake_type::client
                                       , beast::bind_front_handler(&TcpConnection::writeSSL, this)
                                       );
        }
    
        void nextRequest( const int caller
                        , string_view url
                        , string_view data
                        , string_view auth
                        , string_view xApiKey
                        , const Pool::Method method
                        , const Pool::Format format
                        )
        {
            // keepaliveTimer.cancel(); // timer automatically reset!
    
            callerId = caller;

            setupHttpRequest(req, method, url, data, auth, xApiKey, format);
            
            getSock(*stream).expires_after(std::chrono::seconds(30));
    
            if (getSock(*stream).socket().is_open() == false)
                return Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::nextRequest(): sock already closed"));
                
            beast::http::async_write(*stream, req, beast::bind_front_handler(&TcpConnection::read, this));
        }

        void write(beast::error_code ec, asio::ip::tcp::resolver::endpoint_type)
        {
            return writeSSL(ec);
        }

        void writeSSL(beast::error_code ec)
        {
            if (ec)
            {
                Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::connect(): "), ec.message().c_str());
                hostHash->store(0, std::memory_order_relaxed);
                getSock(*stream).socket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                return;
            }
    
            getSock(*stream).expires_after(std::chrono::seconds(30));
    
            beast::http::async_write(*stream, req, beast::bind_front_handler(&TcpConnection::read, this));
        }

        void read(beast::error_code ec, size_t bytes_transferred)
        {
            totalread += (int)bytes_transferred;
            totalConsecutiveReads += 1;

            static constexpr int maxlen = 1*1024*1024;
            static constexpr int maxConsecutiveReads = 16;

            if (totalread > maxlen)
                return Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::read(): maxlen exceeded "), totalread);
            if (totalConsecutiveReads > maxConsecutiveReads)
                return Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::read(): maxreads exceeded "), totalConsecutiveReads);
        
            if (ec)
            {
                Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::read(): "), ec.message().c_str());
                // reset & close:
                hostHash->store(0, std::memory_order_relaxed);
                getSock(*stream).socket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                return;
            }
            
            res.reset();
            res.emplace();
            
            beast::http::async_read(*stream, buffer, *res, beast::bind_front_handler(&TcpConnection::doneRead, this));
        }

        void doneRead(beast::error_code ec, size_t bytes_transferred)
        {
            boost::ignore_unused(bytes_transferred);
            
            if (ec)
            {
                Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::doneRead(): "), ec.message().c_str());
                // reset & close:
                hostHash->store(0, std::memory_order_relaxed);
                getSock(*stream).socket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
                return;
            }
        
            bool wantsKeepalive = res->keep_alive();
            const auto field_keepAlive1 = res->find(PROTECTED("onnection-close"));
            const auto field_keepAlive2 = res->find(PROTECTED("onnection-Close"));
            wantsKeepalive = wantsKeepalive || (field_keepAlive1 == res->end());
            wantsKeepalive = wantsKeepalive || (field_keepAlive2 == res->end());

            [this]
            {
                const lock_guard<mutex> lock(replies_lock);
                replies.emplace( callerId
                               , Reply{ (int)res->result_int()
                                      , res->body()
                                      }
                               );
            }();

            buffer.clear();
        
            busy->makeAvail();

            totalread = 0;
            totalConsecutiveReads = 0;

            if (wantsKeepalive)
                keepAlive(60);
        }

        void keepAlive(const int timeout)
        {
            auto close = [this](boost::system::error_code ec)
                         {
                             getSock(*stream).socket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    
                             if constexpr (this->DoSSL)
                             {
                                 auto newstream = std::make_unique<Sockettype>(make_strand(stream->get_executor()), this->ssl_ctx);
                                 stream.swap(newstream);
                             }
                             else
                             {
                                 auto newstream = std::make_unique<Sockettype>(make_strand(stream->get_executor()));
                                 stream.swap(newstream);
                             }
    
                             // Clear host once disconnected:
                             hostHash->store(0, std::memory_order_relaxed);
                             
                             if (ec)
                                 Debug::print(Debug::Level::warning, DBGSTR("TcpConnection::keepAlive(): "), ec.message().c_str());
                         };

            // todo: chk if closed alrdy!!!
            keepaliveTimer.expires_from_now(boost::posix_time::seconds(timeout));
            keepaliveTimer.async_wait(close);
        }
    
        TcpConnection(const TcpConnection&) = delete;
        TcpConnection& operator=(const TcpConnection&) = delete;

        TcpConnection(TcpConnection&&) = default;
        TcpConnection& operator=(TcpConnection&&) = default;
        
        ~TcpConnection()
        {}
    };


/*****************************************************************************/
/* Connection pool                                                           */ 
/*****************************************************************************/    
    /****************************************/
    /*     Connection pool internal members */
    /****************************************/
    struct Pool::PoolMembers
    {
       // asio::io_context& ioContext;
        
        vector<TcpConnection<beast::tcp_stream, SSL_off>> connections;

        vector<TcpConnection<beast::ssl_stream<beast::tcp_stream>, SSL_on>> connectionsSSL;

        mutex                        replies_lock;
        unordered_map<int, Reply>    replies;
        
        PoolMembers(asio::io_context& ioContext, const int max_concurrent_connections, const int max_concurrent_connections_ssl)
         // : ioContext(ioc)
        {
            for (int i=0; i<max_concurrent_connections; ++i)
                connections.emplace_back(ioContext, replies_lock, replies);
            for (int i=0; i<max_concurrent_connections_ssl; ++i)
                connectionsSSL.emplace_back(ioContext, replies_lock, replies);
        }
        
        PoolMembers(const PoolMembers&) = delete;
        PoolMembers& operator=(const PoolMembers&) = delete;
    };


    /****************************************/
    /*                           Pool impl. */
    /****************************************/ 
    inline bool sslSetup(beast::tcp_stream& sock, void *host)
    {
        boost::ignore_unused(sock, host);
        return true;
    }
    
    inline bool sslSetup(beast::ssl_stream<beast::tcp_stream>& sock, void *host)
    {
        // Set SNI Hostname (many hosts need this to handshake successfully)
        if (!SSL_set_tlsext_host_name(sock.native_handle(), host))
        {
            beast::error_code ec{ static_cast<int>(::ERR_get_error())
                                , boost::asio::error::get_ssl_category() 
                                };
            Debug::print(Debug::Level::error, DBGSTR("Pool::request_internal(): "), ec.message().c_str());
            return false;
        }
    
        return true;
    }
    
    template <bool SSL, bool ConnectSocks5, typename Connection>
    void Pool::request_internal( Connection& connections
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
                               , const Pool::Method method
                               , const Pool::Format format
                               , const unsigned short socks5port
                               )
    {
        std::string_view site{userPwHost, (unsigned)userPwHostLen};
        std::string_view host = site.substr(site.find("@")+1);
    
        // Find if host already connected:
        const u64 hostHash = simplehash(host.data(), (enc_u32)host.size());
        for (auto& connection : connections)
        {
            if (connection.hostHash->load(std::memory_order_relaxed) == hostHash)
            {
                Debug::print(trace, DBGSTR("Pool::request_internal(): Host found. Sending new request "));
                // host found, sending new request:
                if (connection.busy->isAvail_then_lock() == AtomicFlag::State::avail_but_no_more)
                    return connection.nextRequest( callerId
                                                 , string_view{url, (unsigned)urllen}
                                                 , string_view{data, (unsigned)datalen}
                                                 , string_view{auth, (unsigned)authlen}
                                                 , string_view{xApiKey, (unsigned)xApiKeylen}
                                                 , method
                                                 , format
                                                 );
                else
                    return Debug::print(Debug::Level::warning, DBGSTR("Pool::request_internal(): connection busy "), host.data());
            }
        }
        
        // Reuse connection:
        for (auto& connection : connections)
        {
            if (connection.hostHash->load(std::memory_order_relaxed) == 0)
            {
                if constexpr (SSL)
                {
                    if (string h(host); sslSetup(*connection.stream, h.data()) == false)
                        return Debug::print(Debug::Level::warning, DBGSTR("Pool::request_internal(): ssl setup fail"));
                }
                
                return connection.template establish<ConnectSocks5>( callerId
                                                                   , site
                                                                   , string_view{url, (unsigned)urllen}
                                                                   , string_view{data, (unsigned)datalen}
                                                                   , string_view{auth, (unsigned)authlen}
                                                                   , string_view{xApiKey, (unsigned)xApiKeylen}
                                                                   , method
                                                                   , format
                                                                   , socks5port
                                                                   );
            }
        }
        
        Debug::print(Debug::Level::error, DBGSTR("Pool::request_internal(): Exhausted"));

        // Create new connection:
      /*  Debug::print(Debug::Level::trace, DBGSTR("Pool::request_internal(): New connection to: "), host.data());
        connections.emplace_back(pPoolMembers->ioContext, pPoolMembers->replies_lock, pPoolMembers->replies);
        auto& newConn = connections.back(); 
        if constexpr (SSL)
        {
            if (string h(host); sslSetup(*newConn.stream, h.data()) == false)
                return Debug::print(Debug::Level::warning, DBGSTR("Pool::request_internal(): ssl setup fail"));
        }
        newConn.template establish<ConnectSocks5>( callerId
                                                 , site
                                                 , string_view{url, (unsigned)urllen}
                                                 , string_view{data, (unsigned)datalen}
                                                 , string_view{auth, (unsigned)authlen}
                                                 , string_view{xApiKey, (unsigned)xApiKeylen}
                                                 , method
                                                 , format
                                                 , socks5port
                                                 );*/
    }
       
    Pool::Pool(void *global)
      : pPoolMembers(new PoolMembers( ((Global *)global)->ioContext, max_concurrent_connections, max_concurrent_connections_ssl) )
    {}

    void Pool::request( const int callerId
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
                      , const Pool::Method method
                      , const Format format
                      )
    {
        request_internal<false, false>( pPoolMembers->connections
                                      , callerId
                                      , userPwHost
                                      , userPwHostLen
                                      , url
                                      , urllen
                                      , data
                                      , datalen
                                      , auth
                                      , authlen
                                      , xApiKey
                                      , xApiKeylen
                                      , method
                                      , format
                                      );
    }

    void Pool::requestSSL( const int callerId
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
                         , const Pool::Method method
                         , const Format format
                         )
    {
        request_internal<true, false>( pPoolMembers->connectionsSSL
                                     , callerId
                                     , userPwHost
                                     , userPwHostLen
                                     , url
                                     , urllen
                                     , data
                                     , datalen
                                     , auth
                                     , authlen
                                     , xApiKey
                                     , xApiKeylen
                                     , method
                                     , format
                                     );
    }

    void Pool::requestSocks5( const int callerId
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
                            , const Pool::Method method
                            , const Format format
                            , const unsigned short socks5port
                            )
    {
        request_internal<false, true>( pPoolMembers->connections
                                     , callerId
                                     , userPwHost
                                     , userPwHostLen
                                     , url
                                     , urllen
                                     , data
                                     , datalen
                                     , auth
                                     , authlen
                                     , xApiKey
                                     , xApiKeylen
                                     , method
                                     , format
                                     , socks5port
                                     );
    }

    void Pool::requestSocks5SSL( const int callerId
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
                               , const Pool::Method method
                               , const Format format
                               , const unsigned short socks5port
                               )
    {
        request_internal<false, false>( pPoolMembers->connectionsSSL
                                      , callerId
                                      , userPwHost
                                      , userPwHostLen
                                      , url
                                      , urllen
                                      , data
                                      , datalen
                                      , auth
                                      , authlen
                                      , xApiKey
                                      , xApiKeylen
                                      , method
                                      , format
                                      , socks5port
                                      );
    }
    
    void Pool::getReply(const int callerId, void *dst, int& statusCode)
    {
        const lock_guard<mutex> lock(pPoolMembers->replies_lock);

        if (pPoolMembers->replies.count(callerId) == 0)
        {
            Debug::print(Debug::Level::warning, "Pool::getReply() replies: ", pPoolMembers->replies.size());
            statusCode = 999;
            return;
        }

        Reply& found = pPoolMembers->replies.at(callerId);

        string& dst2 = *(string *)dst;

        statusCode = found.http_status;
        swap(dst2, found.reply);

        pPoolMembers->replies.erase(callerId);
    }
    
    Pool::~Pool()
    {
        delete pPoolMembers;
    }
    
