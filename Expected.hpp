#ifndef EXPECTED_HPP_
#define EXPECTED_HPP_

#include <stdexcept>        // for std::invalid_argument
#include <typeinfo>         // for typeid()
#include <utility>
#include <exception>


template <class T> class Expected {
private:
    union {
        T m_value;
        std::exception_ptr m_exception;
    };
    bool m_haveValue;

    Expected() {} // used internally

public:
    // Constructors
    Expected(const T& rhs) : m_value(rhs), m_haveValue(true) {}
    Expected(T&& rhs)
        : m_value(std::move(rhs))
        , m_haveValue(true) {}

    Expected(const Expected& rhs)
        : m_haveValue(rhs.m_haveValue)
    {
        if( m_haveValue ) {
            new(&m_value) T(rhs.m_value);
        } else {
            new(&m_exception) std::exception_ptr(rhs.m_exception);
        }
    }
    Expected(Expected&& rhs)
        : m_haveValue(rhs.m_haveValue)
    {
        if( m_haveValue ) {
            new(&m_value) T(std::move(rhs.m_value));
        } else {
            new(&m_exception) std::exception_ptr(std::move(rhs.m_exception));
        }
    }

    // Since std::exception_ptr has a non-trivial destructor, we need one too.
    ~Expected()
    {
        if( m_haveValue ) {
            m_value.~T();
        } else {
            m_exception.~exception_ptr();
        }
    }

    // Swap with another instance
    void swap(Expected& rhs) {
        if( m_haveValue ) {
            if( rhs.m_haveValue ) {
                using std::swap;
                swap(m_value, rhs.m_value);
            } else {
                auto t = std::move(rhs.m_exception);
                new(&rhs.m_value) T(std::move(m_value));
                new(&m_exception) std::exception_ptr(t);
                std::swap(m_haveValue, rhs.m_haveValue);
            }
        } else {
            if( rhs.m_haveValue ) {
                rhs.swap(*this);
            } else {
                // XXX: m_exception.swap(rhs.m_exception);
                std::swap(m_exception, rhs.m_exception);
                std::swap(m_haveValue, rhs.m_haveValue);
            }
        }
    }

    // Construct from an exception
    template <class E>
    static Expected<T> fromException(const E& exception) {
        if( typeid(exception) != typeid(E) ) {
            throw std::invalid_argument("slicing detected");
        }
        return fromException(std::make_exception_ptr(exception));
    }
    static Expected<T> fromException(std::exception_ptr p) {
       Expected<T> result;
       result.m_haveValue = false;
       new(&result.m_exception) std::exception_ptr(std::move(p));
       return result;
    }
    static Expected<T> fromException() {
       return fromException(std::current_exception());
    }

    // Access
    bool valid() const {
        return m_haveValue;
    }
    T& get() {
        if( !m_haveValue ) std::rethrow_exception(m_exception);
        return m_value;
    }
    const T& get() const {
        if( !m_haveValue ) std::rethrow_exception(m_exception);
        return m_value;
    }

    // Check for a given exception
    template <class E>
    bool hasException() const {
        try {
            if( !m_haveValue ) std::rethrow_exception(m_exception);
        } catch (const E& object) {
            return true;
        } catch (...) {
        }
        return false;
    }

    // Helpful function - useful with, e.g. lambdas.
    template <class F>
    static Expected fromCode(F fun) {
        try {
            return Expected(fun());
        } catch (...) {
            return fromException();
        }
    }
};

#endif //EXPECTED_HPP_
