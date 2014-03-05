#include <gtest/gtest.h>

#include "expected.hpp"


TEST(ExpectedTest, Basic) {
    auto one = Expected<int>::fromException(std::invalid_argument("foo"));
    EXPECT_FALSE(one.valid());

    auto two = Expected<int>(1234);
    EXPECT_TRUE(two.valid());

    auto three = Expected<int>(5678);
    EXPECT_EQ(5678, three.get());

    auto four = Expected<int>::fromException(std::invalid_argument("bar"));
    EXPECT_THROW({
        four.get();
    }, std::invalid_argument);

    auto five = Expected<int>::fromException(std::invalid_argument("baz"));
    EXPECT_TRUE(five.hasException<std::invalid_argument>());
}

TEST(ExpectedTest, fromException) {
    std::invalid_argument e("foo");
    auto one = Expected<int>::fromException(e);
    EXPECT_TRUE(one.hasException<std::invalid_argument>());

    std::invalid_argument f("bar");
    auto two = Expected<int>::fromException( std::make_exception_ptr(f) );
    EXPECT_TRUE(two.hasException<std::invalid_argument>());

    try {
        throw std::invalid_argument("baz");
    } catch( std::invalid_argument& e ) {
        auto three = Expected<int>::fromException();
        EXPECT_TRUE(three.hasException<std::invalid_argument>());
    }
}

TEST(ExpectedTest, fromCode) {
    auto one = Expected<int>::fromCode([] {
        return 1234;
    });
    EXPECT_EQ(1234, one.get());

    // Note: Need to specify this type explicitly since the function
    // never actually returns something that the compiler can infer.
    auto two = Expected<int>::fromCode([]() -> int {
        throw std::invalid_argument("foo");
    });
    EXPECT_TRUE(two.hasException<std::invalid_argument>());
}

TEST(ExpectedTest, constructor) {
    // Test the copy constructor.
    auto one = Expected<int>(1234);
    auto two = Expected<int>(one);
    EXPECT_EQ(1234, two.get());

    // Test the move constructor
    auto three = Expected<int>( Expected<int>(5678) );
    EXPECT_EQ(5678, three.get());
}

TEST(ExpectedTest, swap) {
    // Swap two with values.
    // --------------------------------------------------
    auto one = Expected<int>(1234);
    auto two = Expected<int>(5678);

    one.swap(two);
    EXPECT_EQ(5678, one.get());
    EXPECT_EQ(1234, two.get());

    // Swap one with value, one with exception (both ways)
    // --------------------------------------------------
    auto three = Expected<int>(1234);
    auto four = Expected<int>::fromException(std::invalid_argument("foo"));

    three.swap(four);
    EXPECT_TRUE(three.hasException<std::invalid_argument>());
    EXPECT_EQ(1234, four.get());

    auto five = Expected<int>(1234);
    auto six = Expected<int>::fromException(std::invalid_argument("foo"));

    six.swap(five);
    EXPECT_TRUE(five.hasException<std::invalid_argument>());
    EXPECT_EQ(1234, six.get());

    // Swap two with exceptions
    // --------------------------------------------------
    auto seven = Expected<int>::fromException(std::invalid_argument("foo"));
    auto eight = Expected<int>::fromException(std::length_error("bar"));

    seven.swap(eight);
    EXPECT_TRUE(seven.hasException<std::length_error>());
    EXPECT_TRUE(eight.hasException<std::invalid_argument>());
}
