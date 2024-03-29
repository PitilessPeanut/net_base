#ifndef GLOBAL_HPP
#define GLOBAL_HPP

#include <boost/asio.hpp>
#include <shared_mutex>
#include <atomic>


/*****************************************************************************/
/*                                                           Synchronization */ 
/*****************************************************************************/
    struct AtomicFlag
    {
        static constexpr bool atom_open = false;
        static constexpr bool atom_closed = true;
    
        std::atomic_flag flag;
    
        enum class State { not_avail, avail_but_no_more };
    
        AtomicFlag()
        {
            flag.clear();
        }

        AtomicFlag(const AtomicFlag&) = delete;
        AtomicFlag& operator=(const AtomicFlag&) = delete;
        
        State isAvail_then_lock()
        {
            // https://www.codeproject.com/Articles/1183423/We-Make-a-std-shared-mutex-10-Times-Faster
            // No instr. placed after memory_order_acquire can be executed before it
            // No instr. placed before memory_order_release can be executed after it
            if (flag.test_and_set(std::memory_order_acquire) == atom_open)
                return State::avail_but_no_more;
            return State::not_avail;
        }

        void makeAvail()
        {
            flag.clear(std::memory_order_release);
        }
    };


    struct AtomicBool
    {
        std::atomic<bool> atomicBool;
    
        explicit AtomicBool(bool init)
          : atomicBool(init)
        {}
    
        void operator=(bool val)
        {
            // atomicBool.store(val, std::memory_order_acquire);
            // atomicBool.store(val, std::memory_order_release);
            atomicBool.store(val, std::memory_order_relaxed);
        }
    
        bool operator==(const bool val) const
        {
            return atomicBool.load(std::memory_order_relaxed) == val;
        }
        
        operator bool() const
        {
            return atomicBool.load(std::memory_order_relaxed);
        }
    };


/*****************************************************************************/
/*                     shared_ptr wrapper to prevent non-const member access */
/*****************************************************************************/
    template <class T>
    class SharedPtr
    {
    private:
        std::shared_ptr<T> ptr;
        
    public:
        explicit SharedPtr(std::shared_ptr<T> newPtr)
          : ptr(newPtr)
        {}
        
        T* operator->() const noexcept { return ptr.get(); }
        
        T& operator*() const noexcept { return *ptr.get(); }
    };


/*****************************************************************************/
/*                                                                  Director */ 
/*****************************************************************************/
    /****************************************/
    /* Doom3-style circular list            */
    /* (but with locks)                     */
    /****************************************/
    template <class T> class Circular;

    template <class Ownertype>
    class Node
    {
    public:
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;
        
        Node(Node&&) = default;
        Node& operator=(Node&&) = default;
    
        Node()
          : owner(nullptr)
        {
            prev = this;
            next = this;
        }  
    
        void disconnect()
        {
            std::unique_lock<std::shared_mutex> lock(list->lock);
                    
            list->head = next;
            
            next->prev = prev;
            prev->next = next;
    
            list->cnt -= 1;
    
            if (list->cnt == 0)
                list->head = nullptr;
        }
        
        Ownertype *owner;
        Node<Ownertype> *prev;
        Node<Ownertype> *next;
        Circular<Ownertype> *list = nullptr;
    };


    template <class T>
    class Circular
    {
    public:
        Node<T> *head = nullptr;
    
        int cnt = 0;
        
        std::shared_mutex lock;
    
    public:
        Circular() = default;
        
        Circular(const Circular&) = delete;
        Circular& operator=(const Circular&) = delete;

        void insert(Node<T>& newNode)
        {
            std::unique_lock<std::shared_mutex> l(lock);
            
            if (head)
            {
                Node<T>& first = *head;
                Node<T>& second = *head->next;
    
                newNode.prev = &first;
                newNode.next = &second;
    
                second.prev = &newNode;
                first.next = &newNode;
            }
    
            newNode.list = this;
            head = &newNode;
            cnt += 1;
        }

        Node<T> *next()
        {
            std::shared_lock<std::shared_mutex> l(lock);
            
            if (head)
                head = head->next;
            return head;
        }
    };


    /****************************************/
    /* Task                                 */
    /****************************************/
    struct Task
    {
        SharedPtr<boost::asio::deadline_timer> timer;

        Node<Task> node;
        
        const unsigned taskid;

        Task( Circular<Task>& tasks
            , SharedPtr<boost::asio::deadline_timer> t
            , const unsigned id
            )
          : timer(t)
          , taskid(id)
        {
            node.owner = this;
            tasks.insert(node);
        }
        
        Task(Task&&) = delete;
        Task& operator=(Task&&) = delete;
    
        ~Task()
        {
            node.disconnect();
        }
    };
    
    
    /****************************************/
    /* Event schedule/Director              */
    /****************************************/
    class Director
    {
    private:
        boost::asio::io_context& ioContext;

        Circular<Task> circular;
        
        unsigned taskid = 1;
        
    public:
        explicit Director(boost::asio::io_context& ioCtx)
          : ioContext(ioCtx)
        {}
        
        Director(const Director&) = delete;
        Director& operator=(const Director&) = delete;
    
        unsigned submitTask(const auto cntdwnMilliSecs, SharedPtr<std::function<void()>> dispatch)
        {
            using DeadlineTimer = boost::asio::deadline_timer;
            SharedPtr<DeadlineTimer> timer(std::make_shared<DeadlineTimer>(ioContext, boost::posix_time::milliseconds(cntdwnMilliSecs)));

            SharedPtr<Task> task(std::make_shared<Task>(circular, timer, taskid));
           
            timer->async_wait([dispatch, timer, task](const boost::system::error_code ec)
                              {
                                  if (!ec)
                                      (*dispatch)();
                              }
                             );
            
            return taskid++;
        }
      
        void cancelTask(const unsigned id)
        {
            Node<Task> *head = circular.next();

            if (!head)
                return;

            Node<Task> *current = head;
        
            do
            {
                current = circular.next();
                if (current->owner->taskid == id)
                {
                    current->owner->timer->cancel();
                    return;
                }
            } while (current != head);
        }
        
        void shutdown()
        {
            Node<Task> *head = circular.next();

            if (!head)
                return;

            Node<Task> *current = head;
    
            do
            {
                current = circular.next();
                current->owner->timer->cancel();
            } while (current != head);
        }
    };

    
/*****************************************************************************/
/*                                                              Global state */ 
/*****************************************************************************/
    struct Global
    {
        boost::asio::io_context ioContext;

        std::atomic_int nCurrentConnected;

        Global() : nCurrentConnected(0) {}
        
        Global(const Global&) = delete;
        Global& operator=(const Global&) = delete;
    };

    
#else
  #error "double include"
#endif // GLOBAL_HPP


