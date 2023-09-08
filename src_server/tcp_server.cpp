#include "tcp_server.hpp"
#include "global.hpp"
#include "encode.hpp"
#include "../src/platform.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/algorithm/string.hpp>

using std::vector;
using std::unique_ptr;
using std::shared_ptr;
using std::move;
using std::string;
using std::string_view;
using std::ostringstream;
using std::fill;
using std::enable_shared_from_this;
using std::mutex;
using std::lock_guard;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace chrono = std::chrono;
using enum Debug::Level;

#define STOPWATCH


/*****************************************************************************/
/*                                                                     Guest */
/*****************************************************************************/
    struct Guest::GuestMembers
    {
        u64                     cookieU64;
        AtomicFlag              busy;
        asio::deadline_timer    zombieClock;
        string                  cookie;
    
        explicit GuestMembers(asio::io_context& ioCtx)
          : //busy(AtomicFlag>())
           zombieClock(ioCtx)
          , cookie("c=          ") // Must have '=' somewhere!!
        {
            auto r = pcg64Rand();
            // All letters in the cookie-string are converted to lower-case
            // to speed things up
            cookie[ 2] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[ 3] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[ 4] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[ 5] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[ 6] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[ 7] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[ 8] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[ 9] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[10] = ('0'+(r&7)) & 255;  r >>= 3;
            cookie[11] = ('0'+(r&7)) & 255;
            cookieU64 = simplehash_x86_64(cookie.c_str(), (int)cookie.size());
        }
    };

    Guest::Guest(void *ioCtx)
      : pGuestMembers(new GuestMembers( *(asio::io_context *)ioCtx )) 
      , userid(0)
    {}

    Guest::Guest(Guest&& other)
      : pGuestMembers(std::exchange(other.pGuestMembers, nullptr))
      , userid(other.userid)
      , keepalive(std::exchange(other.keepalive, false))
      , deflateSupported(std::exchange(other.deflateSupported, false))
    {}

    void Guest::swap(Guest& other)
    {
        std::swap(pGuestMembers, other.pGuestMembers);
        std::swap(keepalive, other.keepalive);
        std::swap(deflateSupported, other.deflateSupported);
    }

    Guest::~Guest()
    {
        if (pGuestMembers)
            delete pGuestMembers;
    }


/*****************************************************************************/
/*                                                      'homepage' container */
/*****************************************************************************/
    struct Page_base::PageMembers
    {
        ostringstream chunk;
        string staysAliveOutput;
    
        void makeHttp200_OK()              { chunk << PROTECTED("HTTP/1.1 200 OK\r\n"); }
        void makeHttp404_notFound()        { chunk << PROTECTED("HTTP/1.1 404 Not Found\r\n"); }
        void makeChunked()                 { chunk << PROTECTED("Transfer-Encoding: chunked\r\n"); }
        void placeType(Page_base::Mime m)  { using enum Page_base::Mime;
                                             chunk << PROTECTED("Content-Type: ");
                                             switch (m)
                                             { 
                                                 case HTML: { chunk << PROTECTED("text/html"); } break;
                                                 case CSS : { chunk << PROTECTED("text/css"); } break;
                                                 case JS  : { chunk << PROTECTED("application/javascript"); } break;
                                                 case JSON: { chunk << PROTECTED("application/json"); } break;
                                                 case ICON: { chunk << PROTECTED("image/vnd.microsoft.icon"); } break;
                                                 case PNG : { chunk << PROTECTED("image/png"); } break;
                                                 case FLV : { chunk << PROTECTED("video/x-flv"); } break;
                                                 case JPG : { chunk << PROTECTED("image/jpeg"); } break;
                                                 case GIF : { chunk << PROTECTED("image/gif"); } break;
                                                 case SVG : { chunk << PROTECTED("image/svg+xml"); } break;
                                                 default  : { chunk << PROTECTED("application/text"); }
                                             }
                                             chunk << PROTECTED("; charset=utf-8\r\n");
                                           }
        void placeKeepalive(const bool ka) { ka ? (chunk<<PROTECTED("Connection: keep-alive\r\n")) : (chunk<<PROTECTED("Connection: close\r\n")); } 
        void placeOnion(string_view onion) { chunk << PROTECTED("Onion-Location: ") << onion << PROTECTED("\r\n"); }
        void finishHeader()                { chunk << PROTECTED("\r\n"); }
        void insertBodyChunk(string& body) { chunk << std::hex << body.size() << PROTECTED("\r\n") << body << PROTECTED("\r\n"); }
        
        string getChunk()
        {
            staysAliveOutput = chunk.str();
            ostringstream clean;
            chunk.swap(clean); // Unfortunately this is needed
            return staysAliveOutput;
        }

        PageMembers() {}
    };


    Page_base::Page_base()
      : pPageMembers(new PageMembers(  ))
    {}

    void Page_base::buffers(void *dst, const Guest& guest, const int httpStatus)
    {
        string& dst2 = *(string *)dst;
        string body;
        placeChunk(&body);
        
        if (sendHeader)
        {        
            sendHeader = false;
            done = false;
            if (httpStatus == 200)
                pPageMembers->makeHttp200_OK();
            else if (httpStatus == 404)
                pPageMembers->makeHttp404_notFound();
            pPageMembers->makeChunked();
            pPageMembers->placeType(getType());
          //  pPageMembers->placeCookie(user);//todo
            pPageMembers->placeKeepalive(guest.keepalive);
          //  pPageMembers->placeXSSProtection();//todo
            pPageMembers->finishHeader();
            pPageMembers->insertBodyChunk(body);
            string chk(pPageMembers->getChunk());
            swap(dst2, chk);
        }
        else if (!body.empty())
        {
            pPageMembers->insertBodyChunk(body);
            string chk(pPageMembers->getChunk());
            swap(dst2, chk);
        }
        else
        {
            done = true;
            sendHeader = true;
            string chk(PROTECTED("0\r\n\r\n"));
            swap(dst2, chk);
        }
    }

    Page_base::~Page_base()
    {
        if (pPageMembers)
            delete pPageMembers;
    }


/*****************************************************************************/
/* Http Session                                                              */
/*****************************************************************************/
    /****************************************/
    /*                         helper funcs */
    /****************************************/
    enum class Method { GET,POST,HEAD,BAD,BODY,INDETERMINATE };

    template <std::convertible_to<std::string_view> S>
    inline void toLower(S& input)
    {
        try 
        {
            boost::algorithm::to_lower(input);
        }
        catch (...)
        {
            Debug::print(warning, DBGSTR("toLower(): "), input.c_str());
        }
    }

    inline void restoreUser( asio::io_context& ioContext
                           , vector<Guest>& guests
                           , Guest *& currentGuest
                           , const string& receivedHeader
                           , mutex& guests_lock
                           )
    {
        string_view needle(receivedHeader);
        auto cookiePos = needle.find(PROTECTED("cookie:"));
        cookiePos += 9; // c o o k i e :
                        // 1 2 3 4 5 6 7 8 9
        if ((cookiePos+2) >= needle.size())
            return;
        needle = needle.substr(cookiePos);
        needle.substr(0, needle.find(PROTECTED("\r\n\r\n")));
        const enc_u64 needleU64 = simplehash_x86_64(needle.data(), (int)needle.size());

        [&guests, needleU64, &currentGuest, &guests_lock]
        {
            const lock_guard<mutex> lock(guests_lock);
            for (Guest& guest : guests)
            {
                if (needleU64 == guest.pGuestMembers->cookieU64)
                {
                    if (&guest == currentGuest)
                        return;
                    currentGuest->swap(guest);
                    Debug::print(trace, DBGSTR("restoreUser(): guest found (todo removeme)"));
                    return;
                }
            }
        }();

        for (Guest& guest : guests) // todo chk empty slot (userid == 0)
        {
            if (false)
            {
                return;
            }
        }

        
       // guests.emplace_back( &ioContext );
       // currentGuest = &guests.back();
    }

    inline bool checkKeepalive(const string& receivedHeader)
    {
        constexpr std::array<char, 10> keepalive{'k','e','e','p','-','a','l','i','v','e'};
        return boost::algorithm::contains(receivedHeader, string_view{keepalive.data(), keepalive.size()});
    }

    inline bool checkClose(const string& receivedHeader)
    {
        constexpr std::array<char, 17> connClose{'c','o','n','n','e','c','t','i','o','n',';',' ','c','l','o','s','e'};
        return boost::algorithm::contains(receivedHeader, string_view{connClose.data(), connClose.size()});
    }

    inline int checkContentLength(const string& receivedHeader)
    {
        string_view needle(receivedHeader);
        auto npos = needle.find(PROTECTED("content-length:"));
        if (npos == 0)
            return 0;
        npos += 16; // "content-length" is 14 letters followed by ':' and ' '

        int len=0;
        const auto headerEnd = needle.end()-1;
        auto pos = needle.begin()+npos;
        while ((pos != headerEnd) && isdigit(*pos))
        {
            len *= 10;
            len += (*pos - 48);
            ++pos;
        }
        return len;
    }

    inline bool findRequestedPage(unique_ptr<Page_base>& page, const string& receivedHeader)
    {
        string_view url;
        try
        {
            int GET_offset = 4; // "GET "
            GET_offset += receivedHeader[GET_offset] != '/'; // "POST " etc...
            url = string_view(receivedHeader.c_str()+GET_offset, receivedHeader.size()-GET_offset);
            string urlextracted(PROTECTED("http://a.bc/")); 
            urlextracted += url.substr(url.find('/')+1, url.find(PROTECTED("HTTP"))-2);
            return page->findRequestedPage(urlextracted.c_str(), (int)urlextracted.size());
        }
        catch (std::exception& e)
        {
            Debug::print(trace, DBGSTR("findRequestedPage() exception: "), receivedHeader.c_str());
            return false;
        }
    }

    void clean(string& str)
    {
        boost::replace_all(str, PROTECTED("&")  , PROTECTED("&amp;"));
        boost::replace_all(str, PROTECTED("@")  , PROTECTED("&#64;"));
        boost::replace_all(str, PROTECTED("`")  , PROTECTED("&#96;"));
        boost::replace_all(str, PROTECTED("+")  , PROTECTED("&#43;"));
        boost::replace_all(str, PROTECTED("$")  , PROTECTED("&#36;"));
/* todo: finish
        boost::replace_all(str, PROTECTED("=")  , PROTECTED(""));
        boost::replace_all(str, PROTECTED("{")  , PROTECTED(""));
        boost::replace_all(str, PROTECTED("}")  , PROTECTED(""));
        boost::replace_all(str, PROTECTED("[")  , PROTECTED(""));
        boost::replace_all(str, PROTECTED("]")  , PROTECTED(""));
*/
        boost::replace_all(str, PROTECTED("%")  , PROTECTED("&#35;"));
        boost::replace_all(str, PROTECTED("<")  , PROTECTED("&lt;"));
        boost::replace_all(str, PROTECTED(">")  , PROTECTED("&gt;"));    
        boost::replace_all(str, PROTECTED("\\") , PROTECTED("&#92;"));
        boost::replace_all(str, PROTECTED("/")  , PROTECTED("&#47"));
        boost::replace_all(str, PROTECTED(" ")  , PROTECTED("&nbsp;"));
        boost::replace_all(str, PROTECTED("'")  , PROTECTED("&apos;"));
        boost::replace_all(str, PROTECTED("\"") , PROTECTED("&quot;"));
        boost::replace_all(str, PROTECTED("!")  , PROTECTED("&#33;"));
        boost::replace_all(str, PROTECTED("(")  , PROTECTED("&#40;"));
        boost::replace_all(str, PROTECTED(")")  , PROTECTED("&#41;"));
        boost::replace_all(str, PROTECTED("\r\n"), "&");
    
        // Erase bullshit characters (probs slow...):
        int n=0;
        for (int i=0; i < (int)str.length(); ++i)
            if (str[i] < ' ')
                n++;
            else
                str[i-n] = str[i];
        str.resize(str.length()-n);
    
        // https://bjoern.hoehrmann.de/utf-8/decoder/dfa/
        //todo chk utf8
        //    const auto originalSize = body.size();
        //    body += "    ";
            
        //    for (int i=0; i<body.size()-4; ++i)
        //        if (utf8ok(&body[i]) == false)
        //            body[i] = ' ';
        //    body.resize(originalSize);
    }

    
    /****************************************/
    /*    A single http 'session' per user. */
    /* Only one per user! Sessions is       */
    /* swapped in case a user re-connects   */ 
    /* before time-out                      */
    /****************************************/
    template <class Sockettype>
    class HttpSession : public enable_shared_from_this<HttpSession<Sockettype>>
    {
    private:
        asio::io_context&        ioContext;
        Sockettype               stream; // Constructed via make_strand() no need to use strand::wrap all
                                         // the time any more!
        vector<char>             buffer; // TCP being a stream, block/buffer size has no influence over 
                                         // the packets sent thru the network. Trying to guess the avarage 
                                         // size of a packet: 4096
        size_t                   totalReceived = 0, totalDelivered = 0; 
        std::atomic_int&         nCurrentConnected;
        bool                     body = false;
        int                      endrequest = 0;
        int                      headerLength = 0;
        int                      bodyLength = -1;
        int                      standby = 1;
        int                      httpCode = 200;
        string                   receivedHeader, receivedBody, out;
        bool                     connectionclose = false;
        unique_ptr<Page_base>    page;
        std::mutex&              guests_lock;
        vector<Guest>&           guests;

        #ifdef STOPWATCH
          std::chrono::high_resolution_clock::time_point now;
        #endif
                                       
        template <typename InputIterator>
        inline Method parse(InputIterator begin, InputIterator end)
        {
            using enum Method;
            Method result = INDETERMINATE;
            // If buffer[0] is not 'G', 'P' or 'H' the rest of this check will be skipped:
            /**/ if (buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T')
                result = GET;
            else if (buffer[0] == 'P' && buffer[1] == 'O' && buffer[2] == 'S' && buffer[3] == 'T')
                result = POST;
            else if (buffer[0] == 'H' && buffer[1] == 'E' && buffer[2] == 'A' && buffer[3] == 'D')
                result = HEAD;
            else if (!body)
                return BAD;
    
            while (begin != end)
            {
                const auto input = *begin++;
                if      (input == '\r' && endrequest == 0) endrequest = 1;
                else if (input == '\n' && endrequest == 1) endrequest = 2;
                else if (input == '\r' && endrequest == 2) endrequest = 3;
                else if (input == '\n' && endrequest == 3) return result;
                else endrequest = 0;
    
                headerLength += 1;
            }
    
            return INDETERMINATE;        
        }
        
        void doReadSome()
        {
            fill(buffer.begin(), buffer.end(), 0); // Prevent inf loop

            shared_ptr<HttpSession<Sockettype>> self = this->shared_from_this();
            auto handleRead = [this, self](beast::error_code ec, size_t bytes_transferred)
                              {
                                  if (ec == beast::http::error::end_of_stream)
                                      return doClose(); // they closed

                                  const/*expr*/ bool timedOut = ec==asio::error::timed_out;
                                  const/*expr*/ bool aborted = ec==asio::error::operation_aborted;
                                  const/*expr*/ bool eof = ec==asio::error::eof;
                                  if (ec && (!timedOut) && (!aborted) && (!eof))
                                  {
                                      doClose();
                                      return Debug::print(warning, DBGSTR("HttpSession::doReadSome(): "), ec.message().c_str());
                                  }

                                  using enum Method;
                                  Method method = INDETERMINATE;
                                  totalReceived += bytes_transferred;
                                  
                                  if (!body)
                                  {
                                      method = parse(buffer.data(), buffer.data() + bytes_transferred);

                                      if (receivedHeader.capacity() < totalReceived)
                                          receivedHeader.reserve(bytes_transferred);

                                      move( buffer.begin()                // Move contents from read buffer
                                          , buffer.begin() + headerLength // to the received vch. Number of
                                          , back_inserter(receivedHeader) // bytes == header size
                                          ); 
                                      
                                      // Quickly find requested page:
                                      if ((page->found()==false) && (receivedHeader.size()>128))
                                      {
                                          if (findRequestedPage(page, receivedHeader) == false)
                                              httpCode = 404;
                                      }
                                      // Check if too much:
                                      else if (page->maxLenExceeded((int)totalReceived))
                                      {
                                          method = BAD;
                                          Debug::print(warning, DBGSTR("HttpSession<Sockettype>::doReadSome(): maxLenExceeded: "), totalReceived);
                                      }
    
                                      receivedBody.reserve(bytes_transferred - headerLength); // Maybe there is a body too
    
                                      move( buffer.begin() + headerLength      // The remaining bytes (if any) is moved
                                          , buffer.begin() + bytes_transferred // into the 'body'
                                          , back_inserter(receivedBody)
                                          );
    
                                      headerLength = 0; // Reset lenght. We want the correct cutoff point where the 'body' starts
                                  }
                                  else if (body)
                                  {
                                      if (receivedBody.capacity() < totalReceived) // todo check if capaz if correct here!
                                          receivedBody.reserve(bytes_transferred);

                                      move( buffer.begin()                     // Move contents from the read buffer
                                          , buffer.begin() + bytes_transferred // to the received body, same like above
                                          , back_inserter(receivedBody)
                                          );
                                      
                                      method = BODY;
                                  }

                                  //  G E T  //
                                  if (method == GET)
                                  {                                 
                                      #ifdef STOPWATCH
                                        now = chrono::high_resolution_clock::now();
                                      #endif

                                      toLower(receivedHeader);
                                      
                                      restoreUser(ioContext, guests, guest, receivedHeader, guests_lock);
                                     
                                      if (guest->keepalive == false)
                                      {
                                          if (checkKeepalive(receivedHeader))
                                          {
                                              guest->keepalive = true;
                                              boost::beast::get_lowest_layer(stream).socket().set_option(asio::socket_base::keep_alive(true));
                                              const int timeout = std::min(90, (TcpServer::max_concurrent_connections/(nCurrentConnected.load(std::memory_order_relaxed)+1)) + 15);
                                              boost::beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(timeout));
                                          }
                                      }
    
                                      connectionclose = checkClose(receivedHeader);
    /*todo
                                      if (guest.pUser->deflateSupported == false)
                                          guest.pUser->deflateSupported = checkDeflate(receivedHeader);
                                          */

                                      doWrite();
                                  }

                                  //  P O S T  //
                                  else if (method == POST)
                                  {
                                      #ifdef STOPWATCH
                                        now = chrono::high_resolution_clock::now();
                                      #endif

                                      toLower(receivedHeader);
                                      
                                      restoreUser(ioContext, guests, guest, receivedHeader, guests_lock);
                                      
                                      if (guest->keepalive == false)
                                          if (checkKeepalive(receivedHeader))
                                          {
                                              guest->keepalive = true;
                                              boost::beast::get_lowest_layer(stream).socket().set_option(asio::socket_base::keep_alive(true));
                                              // If post, reset expires timer to like 120 sec
                                              const int timeout = std::min(120, (TcpServer::max_concurrent_connections/(nCurrentConnected.load(std::memory_order_relaxed)+1)) + 15);
                                              boost::beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(timeout));
                                          }
    
                                      connectionclose = checkClose(receivedHeader);
    
                                     //todo if (guest.pUser->deflateSupported == false)
                                       //   guest.pUser->deflateSupported = checkDeflate(receivedHeader);
                                                                        
                                      if (bodyLength == -1)
                                          bodyLength = checkContentLength(receivedHeader);

                                      /*if (bodyLength == 0)
                                      {
                                      Debug::print("3333 ", receivedBody.size(), "  ", bodyLength, " ", receivedHeader.c_str());
                                          page->submitBody(receivedBody.c_str(), (int)receivedBody.size());
                                          self->doWrite();
                                      }*/
                                      if ((bodyLength+1) == (int)receivedBody.size())
                                      {
                                          page->submitBody(receivedBody.c_str(), (int)receivedBody.size());
                                          self->doWrite();
                                      }
                                      // "Content-Length" header missing
                                      else if (bodyLength<2)
                                      {
                                          page->submitBody(receivedBody.c_str(), (int)receivedBody.size());
                                          self->doWrite();
                                      }
                                      // Continue reading body
                                      else
                                      {
                                          body = true;
                                          self->doReadSome();
                                      }
                                  }
    
                                  //  B O D Y  //
                                  else if (method == BODY)
                                  {   
                                      if ((bodyLength+1) == (int)receivedBody.size())
                                      {
                                          page->submitBody(receivedBody.c_str(), (int)receivedBody.size());
                                          self->doWrite();
                                      }
                                      // Continue reading body
                                      else
                                      {
                                          self->doReadSome();
                                      }                                  
                                  }
/*
                                  //  H E A D  //
                                  else if (method == HEAD)
                                  {
                                      // todo: when do i have time 4 this? :(
                                      self->doClose();
                                  }
*/
                              
                                  else
                                  {
                                      body = false;
                                      doReadSome();
                                  }
                              };

            const int timeout = (std::min(60, (TcpServer::max_concurrent_connections/(nCurrentConnected+1)) + 5) * standby) + 1;
            boost::beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(timeout));
    
            standby = 0;

            stream.async_read_some(asio::buffer(buffer, buffer.size()), handleRead);
        }

        void doWrite()
        {
            shared_ptr<HttpSession<Sockettype>> self = this->shared_from_this();
            auto handleWrite = [this, self](beast::error_code ec, size_t bytes_transferred)
                               {
                                   // write unlock 
                                   totalDelivered += bytes_transferred;

                                   constexpr size_t eightEmBee = 1024*1024*8;
                                   if (totalDelivered>eightEmBee)
                                   {
                                       doClose();
                                       return Debug::print(warning, DBGSTR("HttpSession<Sockettype>::doWrite(): totalDelivered > max"));
                                   }
                                   
                                   if (ec)
                                   {
                                       doClose();
                                       return Debug::print(warning, DBGSTR("HttpSession<Sockettype>::doWrite(): "), ec.message().c_str());
                                   }

                                   if (connectionclose)
                                       return doClose();
                                   
                                   if (page->delivered() == false)
                                       doWrite();
                                   else if (guest->keepalive)
                                   {
                                       #ifdef STOPWATCH
                                         Debug::print( trace, DBGSTR("HttpSession<Sockettype>::doWrite() elapsed: ")
                                                     , (int)duration_cast<chrono::microseconds>(chrono::high_resolution_clock::now()-now).count()
                                                     , DBGSTR(" µs") 
                                                     );
                                       #endif

                                       reset();
                                       doReadSome(); // Wait for next read
                                   }
                                   else
                                       doClose();
                               };

            const int timeout = std::min(10, (TcpServer::max_concurrent_connections/(nCurrentConnected+1)) + 7);
            boost::beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(timeout));

            page->buffers(&out, *guest, httpCode);
         
            // write lock  
            async_write(stream, asio::buffer(out), handleWrite);  

        }
        
        void doClose()
        {
            beast::error_code ec;
            stream.socket().shutdown(asio::ip::tcp::socket::shutdown_send, ec);
        }
        
    public:
        Guest *guest;

        HttpSession( asio::io_context& ioCtx
                   , asio::ip::tcp::socket&& sock
                   , std::atomic_int& connected
                   , std::mutex& lock
                   , vector<Guest>& g
                   , Page_base *page_base
                   , Guest *newguest
                   )
          : ioContext(ioCtx)
          , stream(move(sock))
          , buffer(4096)
          , nCurrentConnected(connected)
          , page(page_base->createNew())
          , guests_lock(lock)
          , guests(g)
          , guest(newguest)
        {}

        HttpSession(const HttpSession&) = delete;
        HttpSession& operator=(const HttpSession&) = delete;

        HttpSession(HttpSession&& other)
          : ioContext(other.ioContext)
          , stream(move(other.stream))
          , buffer(move(other.buffer))
          , nCurrentConnected(other.nCurrentConnected)
          , page(move(other.page))
          , guests_lock(other.guests_lock)
          , guests(other.guests)
          , guest(move(other.guest))
        {}
        
        HttpSession& operator=(HttpSession&&) = delete;

        void start()
        {
            shared_ptr<HttpSession<Sockettype>> self = this->shared_from_this();
            asio::dispatch( stream.get_executor(), [self]{ self->doReadSome(); });
        }

        void reset()
        {
            totalReceived = 0;
            totalDelivered = 0;
            body = false;
            endrequest = 0;
            headerLength = 0;
            bodyLength = -1;
            standby = 1;
            httpCode = 200;
            receivedHeader.clear();
            receivedBody.clear();
            page->reset();
        }

        ~HttpSession()
        {
            // lock guard Todo: if this session gets destroyed before the connection is shut down we will get constant "broken pipe" errors!!!!
            nCurrentConnected -= 1;
            //Debug::print(trace, DBGSTR("HttpSession::~HttpSession(): Connection closed. Remaining: "), nCurrentConnected.load(std::memory_order_relaxed));
        }
    };


/*****************************************************************************/
/*****************************************************************************/
    struct TcpServer::TcpServerMembers
    {
        asio::io_context& ioContext;

        boost::asio::ip::tcp::acceptor acceptor;

        std::mutex       guests_lock;
        vector<Guest>    guests;

        std::atomic_int& nCurrentConnected;

        TcpServerMembers(asio::io_context& ioCtx, std::atomic_int& connected)
          : ioContext(ioCtx)
          , acceptor(asio::make_strand(ioCtx))
          , nCurrentConnected(connected)
        {}
    };

    void TcpServer::doAccept()
    {
        if (!pTcpServerMembers->acceptor.is_open())
            return;

        auto accept = [this](beast::error_code ec, asio::ip::tcp::socket sock)
                      {
                          if (!ec)
                          {
                              // todo: block ip!
                              Guest *newguest = [this]
                                                {
                                                    const lock_guard<mutex> lock(pTcpServerMembers->guests_lock);
                                                    for (auto& guest : pTcpServerMembers->guests)
                                                    {
                                                        if (guest.userid == 0)
                                                        {
                                                            // Alternatively guests should assign their OWN userid (during 1st request!)
                                                            guest.userid = (decltype(guest.userid))pcg64Rand();
                                                            return &guest;
                                                        }
                                                    }
                                                    pTcpServerMembers->guests.emplace_back( &pTcpServerMembers->ioContext );
                                                    return &pTcpServerMembers->guests.back();
                                                }();
                              
                              pTcpServerMembers->nCurrentConnected += 1;
                              shared_ptr<HttpSession<beast::tcp_stream>> spawn = 
                                  make_shared<HttpSession<beast::tcp_stream>>( pTcpServerMembers->ioContext
                                                                             , move(sock)
                                                                             , pTcpServerMembers->nCurrentConnected
                                                                             , pTcpServerMembers->guests_lock
                                                                             , pTcpServerMembers->guests
                                                                             , page_base
                                                                             , newguest
                                                                             );
                              spawn->start();


 
                              /*for (auto& session : pTcpServerMembers->sessions)
                              //for (int i=0; i<pTcpServerMembers->sessions.size(); ++i)
                              {
                                //  auto& session = pTcpServerMembers->sessions[i];
                                  if (session.busy->isAvail_then_lock() == AtomicFlag::State::avail_but_no_more)
                                  {
                                  //Debug::print("reusing sess:: ", i);
                                      session.start(move(sock));
                                      pTcpServerMembers->nCurrentConnected += 1;
                                      //return doAccept(); // Another connection
                                      break;
                                  }
                              }*/
                              
                            /*  constexpr int max_avail_connections = max_concurrent_connections*5/100; // leave 5% open!
                              if (pTcpServerMembers->nCurrentConnected <= max_avail_connections)
                              {
                                  pTcpServerMembers->sessions.emplace_back( pTcpServerMembers->nCurrentConnected
                                                                          , pTcpServerMembers->guests
                                                                          , pTcpServerMembers->sessions
                                                                          , page_base
                                                                          );
                                  auto& newSess = pTcpServerMembers->sessions.back(); 
                                  auto flag = newSess.busy->isAvail_then_lock();
                                  boost::ignore_unused(flag);
                                  newSess.start(move(sock));
                                  pTcpServerMembers->nCurrentConnected += 1;
                                  Debug::print(trace, DBGSTR("TcpServer::doAccept() new session alloc. Total in use: "), pTcpServerMembers->sessions.size());
                              }
                              else
                              {*/
                                 /* for (auto& session : pTcpServerMembers->sessions)
                                  {
                                      if (session.zombie == true)
                                      {
                                          Debug::print( warning, DBGSTR("TcpServer::doAccept() yanking zombie! Connected: ")
                                                      , pTcpServerMembers->nCurrentConnected.load(std::memory_order_relaxed)
                                                      );
                                          session.start(move(sock));
                                          return doAccept(); // Another connection
                                      }
                                  }*/
                               //   Debug::print(error, DBGSTR("TcpServer::doAccept(): Server full!!"));
                             // }
                          }
                          else
                          {
                              Debug::print(warning, DBGSTR("TcpServer::doAccept(): "), ec.message().c_str());
                          }

                          doAccept(); // Another connection
                      };

        pTcpServerMembers->acceptor.async_accept( asio::make_strand(pTcpServerMembers->ioContext)
                                                , accept
                                                );
    }
    
    TcpServer::TcpServer(void *global, Page_base *page)
      : pTcpServerMembers(new TcpServerMembers( ((Global *)global)->ioContext, ((Global *)global)->nCurrentConnected ))
      , page_base(page)
    {
        //pTcpServerMembers->sessions.reserve(max_concurrent_connections);
        //for (int i=0; i<max_concurrent_connections; ++i)
         //   pTcpServerMembers->sessions.emplace_back( pTcpServerMembers->nCurrentConnected
           //                                         , pTcpServerMembers->sessions
             //                                       , page_base
               //                                     );

                                                                          
        const auto address = asio::ip::make_address(PROTECTED("127.0.0.1"));
        const unsigned short port = 8080;
        asio::ip::tcp::endpoint endpoint{address, port};
        beast::error_code ec;
        for (;;)
        {
            pTcpServerMembers->acceptor.open(endpoint.protocol(), ec);
            if (ec)
                break;
            /* Bind several processes to the same UDP port and they will all receive the same message arriving at that port */
            pTcpServerMembers->acceptor.set_option(asio::socket_base::reuse_address(true), ec);
            if (ec)
                break;
            pTcpServerMembers->acceptor.bind(endpoint, ec);
            if (ec)
                break;
            pTcpServerMembers->acceptor.listen(asio::socket_base::max_listen_connections, ec);
            break;
        }
        if (ec)
        {
            Debug::print(error, DBGSTR("TcpServer::TcpServer(): "), ec.message().c_str());
            throw std::runtime_error(DBGSTR("Failed to create server"));
        }
    }

    void TcpServer::go()
    {
        asio::dispatch( pTcpServerMembers->acceptor.get_executor()
                      , beast::bind_front_handler(&TcpServer::doAccept, this)
                      );
    }

    void TcpServer::shutdown()
    {
        pTcpServerMembers->acceptor.close();
    }

    TcpServer::~TcpServer()
    {
        if (pTcpServerMembers)
            delete pTcpServerMembers;
    }

