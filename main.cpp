#include "OptionParser.hpp"
#include "SSL.hpp"
#include "ScopeGuard.hpp"
#include "ThreadPool.h"
#include "cpplog.hpp"

#include <iostream>
#include <unordered_map>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <boost/lexical_cast.hpp>


typedef std::vector<ssl::SSLCipher> CipherList;


static const char* const VERSION = "0.0.1";
static const std::unordered_map<const ::SSL_METHOD*, const char*> ssl_methods({
    {::SSLv2_method(), "SSLv2"},
    {::SSLv3_method(), "SSLv3"},
    {::TLSv1_method(), "TLSv1"},
    {::TLSv1_1_method(), "TLSv1.1"},
    {::TLSv1_2_method(), "TLSv1.2"},
});


class ConcurrentLogger : public cpplog::BaseLogger {
public:
    virtual bool sendLogMessage(LogData* logData)
    {
        bool deleteMessage = true;

        deleteMessage = deleteMessage && m_logger1->sendLogMessage(logData);
        deleteMessage = deleteMessage && m_logger2->sendLogMessage(logData);

        return deleteMessage;
    }
};


class AddressError : public std::runtime_error {
private:
    char m_whatText[200];
public:
    AddressError(int status)
        : std::runtime_error("address error")
    {
        boost::iostreams::stream<boost::iostreams::array_sink> out(m_whatText, 200);
        out << "error resolving address: "
            << status
            << " (" << gai_strerror(status) << ")"
            << std::ends;
    }

    virtual const char* what() const noexcept override {
        return m_whatText;
    }
};


class SocketError : public std::runtime_error {
private:
    char m_whatText[200];
public:
    SocketError()
        : std::runtime_error("socket error")
    {
        int err = errno;

        char errorBuff[200+1];
        strerror_r(err, errorBuff, 200);

        boost::iostreams::stream<boost::iostreams::array_sink> out(m_whatText, 200);
        out << "socket error: "
            << err
            << " (" << errorBuff << ")"
            << std::ends;
    }

    virtual const char* what() const noexcept override {
        return m_whatText;
    }
};


class SocketAddress {
private:
    struct addrinfo m_address;

public:
    static Expected<std::vector<SocketAddress>> ResolveHost(
            std::string host,
            std::string service = "",
            int family = AF_UNSPEC)
    {
        const char* theService;

        if( 0 == service.length() ) {
            theService = "443";
        } else {
            theService = service.c_str();
        }

        struct addrinfo hints;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = family;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* addresses = nullptr;
        int status = getaddrinfo(host.c_str(), theService, &hints, &addresses);
        if( status != 0 ) {
            return Expected<std::vector<SocketAddress>>::fromException(AddressError(status));
        }

        SCOPE_EXIT {
            if( addresses != nullptr ) {
                freeaddrinfo(addresses);
            }
        };

        std::vector<SocketAddress> ret;
        for( struct addrinfo *p = addresses;
             p != nullptr;
             p = p->ai_next )
        {
            ret.emplace_back(p);
        }

        return ret;
    }

    explicit SocketAddress(struct addrinfo* address)
        : m_address(*address)
    { }

    // Operators to convert to an addrinfo structure / pointer.
    operator struct addrinfo ()
    {
        return m_address;
    }
    operator struct addrinfo* ()
    {
        return &m_address;
    }

    // Accessor functions for the members.
    inline int              ai_flags() const     { return m_address.ai_flags; }
    inline int              ai_family() const    { return m_address.ai_family; }
    inline int              ai_socktype() const  { return m_address.ai_socktype; }
    inline int              ai_protocol() const  { return m_address.ai_protocol; }
    inline socklen_t        ai_addrlen() const   { return m_address.ai_addrlen; }
    inline struct sockaddr *ai_addr() const      { return m_address.ai_addr; }
    inline char            *ai_canonname() const { return m_address.ai_canonname; }
};


class Socket {
private:
    int m_socketDescriptor;

public:
    Socket(int family, int socktype, int protocol)
        : m_socketDescriptor(-1)
    {
        int socketDescriptor = -1;

        socketDescriptor = socket(family, socktype, protocol);
        if( -1 == socketDescriptor ) {
            throw SocketError();
        }
        m_socketDescriptor = socketDescriptor;
    }

    // Allow creating with the proper params from a resolved address.
    explicit Socket(SocketAddress& fromAddr)
        : Socket(fromAddr.ai_family(),
                 fromAddr.ai_socktype(),
                 fromAddr.ai_protocol())
    { }

    Socket(Socket&& other)
        : m_socketDescriptor(-1)
    {
        std::swap(m_socketDescriptor, other.m_socketDescriptor);
    }

    virtual ~Socket()
    {
        if( -1 != m_socketDescriptor ) {
            close(m_socketDescriptor);
        }
    }

    Socket& operator=(Socket&& other)
    {
        if( this != &other ) {
            // TODO: abstract from destructor
            if( -1 != m_socketDescriptor ) {
                close(m_socketDescriptor);
                m_socketDescriptor = -1;
            }

            // Swap the two descriptors
            std::swap(m_socketDescriptor, other.m_socketDescriptor);
        }

        return *this;
    }

    // Delete copy constructor and assignment.
    // We want it to be impossible to copy an open socket, though we
    // allow moving one.
    Socket(Socket const&) = delete;
    Socket& operator=(Socket const&) = delete;

    // TODO: bind to local address

    void Connect(SocketAddress& addr)
    {
        int status = connect(m_socketDescriptor, addr.ai_addr(), addr.ai_addrlen());
        if( -1 == status ) {
            throw SocketError();
        }
    }

    int GetFd() const
    {
        return m_socketDescriptor;
    }
};


Expected<Socket> createAndConnectSocket(std::string& host)
{
    auto addresses = SocketAddress::ResolveHost(host);
    if( !addresses.valid() ) {
        return Expected<Socket>::fromException(addresses);
    }

    // For each address, we try and create a socket.
    for( auto& addr: addresses.get() ) {
        try {
            Socket sock(addr);

            sock.Connect(addr);

            // If we get here, no error - so return it.
            return std::move(sock);
        } catch( const SocketError& e ) {
            std::cerr << e.what() << std::endl;
            continue;
        }
    }

    // TODO: better exception here
    return Expected<Socket>::fromException(
            std::runtime_error("couldn't create"));
}


void scanOneHost(std::string host,
                 std::unordered_map<const ::SSL_METHOD*, CipherList>& ciphers)
{
    std::cout << "Scanning: " << host << std::endl;

    auto maybeSock = createAndConnectSocket(host);
    if( !maybeSock.valid() ) {
        std::cerr << "Error connecting" << std::endl;
        return;
    }
    Socket& sock = maybeSock.get();

    // This is a (very) simple string we send to the remote end to
    // verify that the connection is working.
    std::string requestString = "GET / HTTP/1.1\r\n"
                                "User-Agent: SSLScan\r\n"
                                "Host: " + host + "\r\n\r\n";

    for( auto it: ciphers ) {
        for( auto cipher: it.second ) {
            /* std::cout << ssl_methods.at(it.first) */
            /*           << ": " */
            /*           << cipher.Name() */
            /*           << std::endl; */

            ssl::SSLContext ctx(it.first);
            ctx.SetCipherList(cipher.Name());

            ssl::SSL ssl(ctx);

            // TODO: connect to the server and actually test.

        }
    }
}


CipherList getSupportedCiphers(const SSL_METHOD* method) {
    CipherList res;

    ssl::SSLContext ctx(method);
    ctx.SetCipherList("ALL:COMPLEMENTOFALL");

    ssl::SSL ssl(ctx);
    return ssl.GetCipherList();
}


int main(int argc, char* argv[]) {
    std::cout << "SSLScan-cpp v" << VERSION << ", (c) 2014 Andrew Dunham" << std::endl;

    int verbosity = 0,
        threads = 5;

    OptionParser parser;

    parser.On("v").SetCallback([&verbosity]() {
        verbosity++;
    });
    parser.On("t", "threads")
          .SetParameter(true)
          .SetParameterOptional(false)
          .SetCallback([&threads](const std::string& arg)
    {
        try {
            threads = boost::lexical_cast<int>(arg);
            std::cout << "Scanning with " << threads << " threads" << std::endl;
        } catch( const boost::bad_lexical_cast& ) {
            std::cerr << "Invalid value for 'threads': '" << arg << "'" << std::endl;
        }
    });

    Expected<std::vector<std::string>> args = parser.Parse(argc, argv);
    if( !args.valid() ) {
        std::cerr << "Error parsing" << std::endl;

        // We rethrow here and print the error if it's something we can deal with.
        try {
            args.get();
        } catch( const OptionParserError& e ) {
            std::cerr << e.what() << std::endl;
        }

        return 1;
    }

    // Init. SSL
    SSL_library_init();
    SSL_load_error_strings();

    std::unordered_map<const ::SSL_METHOD*, CipherList> ciphers;
    for( auto it: ssl_methods ) {
        std::cout << "Getting ciphers for: " << it.second << std::endl;

        std::vector< ssl::SSLCipher > currCiphers;
        try {
            ciphers.emplace(it.first, getSupportedCiphers(it.first));
        } catch( ssl::SSLError& e ) {
            std::cerr << e.what() << std::endl;
            return 2;
        }
    }

    // Do the scanning.  The ThreadPool will wait on all threads on destruction,
    // so we do this in a new scope.
    // NOTE: It's safe for us to pass unordered_maps in here, so long as we
    // DON'T mutate it - STL containers should allow for concurrent reading, so
    // long as there's no mutation involved.
    {
        ThreadPool pool(threads);

        // TODO: parallelize by host & SSL method, not (just) by host
        for( auto host : args.get() ) {
            pool.enqueue(scanOneHost, host, ciphers);
        }
    }

    std::cout << "Done!" << std::endl;

    return 0;
}
