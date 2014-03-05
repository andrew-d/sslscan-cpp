#ifndef SSL_H
#define SSL_H

#include <vector>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/pkcs12.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/array.hpp>

namespace ssl {

    class SSLError : public std::runtime_error {
    private:
        char m_whatText[1024];
    public:
        SSLError(const char* err)
            : std::runtime_error("ssl error")
        {
            boost::iostreams::stream<boost::iostreams::array_sink> out(m_whatText, (sizeof(m_whatText) / sizeof(m_whatText[0])));
            out << "ssl error " << err << ":";

            unsigned long errCode;
            char errBuff[120+1];
            for( int i = 1; ; i++ ) {
                errCode = ERR_get_error();
                if( 0 == errCode ) {
                    break;
                }

                ERR_error_string(errCode, errBuff);
                out << "\n  error #" << i << ": " << errCode << " (" << errBuff << ")";
            }

            out << std::ends;
        }

        virtual const char* what() const noexcept override {
            return m_whatText;
        }
    };


    class SSLContext {
    private:
        ::SSL_CTX*          m_ctx;
        const ::SSL_METHOD* m_method;

    public:
        explicit SSLContext(const ::SSL_METHOD* method)
            : m_method(method)
        {
            m_ctx = ::SSL_CTX_new(method);
            if( !m_ctx ) {
                throw SSLError("error making context");
            }
        }

        virtual ~SSLContext() {
            if( m_ctx ) {
                ::SSL_CTX_free(m_ctx);
            }
        }

        // Delete copy constructor and assignment.
        SSLContext(SSLContext const&) = delete;
        SSLContext& operator=(SSLContext const&) = delete;

        // Allow move construction
        SSLContext(SSLContext&& other)
            : m_ctx(nullptr), m_method(nullptr)
        {
            m_ctx = other.m_ctx;
            m_method = other.m_method;

            other.m_ctx = nullptr;
            other.m_method = nullptr;
        }

        bool SetCipherList(const char* ciphers) {
            return ::SSL_CTX_set_cipher_list(m_ctx, ciphers) == 1 ? true : false;
        }

        const ::SSL_METHOD* GetMethod() const {
            return m_method;
        }

        operator ::SSL_CTX*() const {
            return m_ctx;
        }
    };


    class SSLCipher {
    private:
        ::SSL_CIPHER* m_cipher;

    public:
        explicit SSLCipher(::SSL_CIPHER* cipher)
            : m_cipher(cipher) {}

        const char* Name() const {
            return ::SSL_CIPHER_get_name(m_cipher);
        }

        const char* Version() const {
            return ::SSL_CIPHER_get_version(m_cipher);
        }

        int Bits() const {
            return ::SSL_CIPHER_get_bits(m_cipher, nullptr);
        }

        // TODO: description?
    };


    class SSL {
    private:
        ::SSL* m_ssl;
        SSLContext& m_context;

    public:
        explicit SSL(SSLContext& context)
            : m_context(context)
        {
            m_ssl = ::SSL_new(m_context);
            if( !m_ssl ) {
                // TODO: error stack
                throw SSLError("error making SSL");
            }
        }

        virtual ~SSL() {
            if( m_ssl ) {
                ::SSL_free(m_ssl);
            }
        }

        // Delete copy constructor and assignment.
        SSL(SSL const&) = delete;
        SSL& operator=(SSL const&) = delete;

        // Get the list of ciphers supported.
        std::vector< SSLCipher > GetCipherList() const {
            std::vector< SSLCipher > ret;
            auto cipherList = ::SSL_get_ciphers(m_ssl);

            for( int i = 0; i < sk_SSL_CIPHER_num(cipherList); i++ ) {
                ret.emplace_back(sk_SSL_CIPHER_value(cipherList, i));
            }

            return ret;
        }

        operator ::SSL* () {
            return m_ssl;
        }
    };
}

#endif
