#include "SSL.hpp"
#include "OptionParser.hpp"
#include "ThreadPool.h"

#include <iostream>
#include <unordered_map>


typedef std::vector<ssl::SSLCipher> CipherList;


static const char* const VERSION = "0.0.1";
static const std::unordered_map<const ::SSL_METHOD*, const char*> ssl_methods({
    {::SSLv2_method(), "SSLv2"},
    {::SSLv3_method(), "SSLv3"},
    {::TLSv1_method(), "TLSv1"},
    {::TLSv1_1_method(), "TLSv1.1"},
    {::TLSv1_2_method(), "TLSv1.2"},
});


void scanOneHost(std::string host,
                 std::unordered_map<const ::SSL_METHOD*, CipherList>& ciphers)
{
    std::cout << "Scanning: " << host << std::endl;

    std::string requestString;
    requestString = "GET / HTTP/1.1\r\n"
                    "User-Agent: SSLScan\r\n"
                    "Host: " + host + "\r\n\r\n";

    for( auto it: ciphers ) {
        for( auto cipher: it.second ) {
            std::cout << ssl_methods.at(it.first)
                      << ": "
                      << cipher.Name()
                      << std::endl;

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

    OptionParser parser;
    parser.On("v", []() {
        std::cout << "Being verbose" << std::endl;
    });

    std::vector<std::string> args;
    try {
        args = parser.Parse(argc, argv);
    } catch( OptionParserError& e ) {
        std::cerr << "Error parsing: " << e.what() << std::endl;
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
        ThreadPool pool(5);

        // TODO: parallelize by host & SSL method, not (just) by host
        for( auto host : args ) {
            pool.enqueue(scanOneHost, host, ciphers);
        }
    }

    std::cout << "Done!" << std::endl;

    return 0;
}
