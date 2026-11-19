#include "test_framework.h"
#include "util/StringUtil.h"

TEST_CASE(fmt_basic)
{
    CHECK_EQ(fmt("a=%1 b=%2", 1, "x"), "a=1 b=x");
    CHECK_EQ(fmt("%1%2", "ab", "cd"), "abcd");
    CHECK_EQ(fmt("no args"), "no args");
}

TEST_CASE(split_basic)
{
    auto v = split("a,b,c", ',');
    CHECK(v.size() == 3);
    CHECK_EQ(v[0], "a");
    CHECK_EQ(v[1], "b");
    CHECK_EQ(v[2], "c");
}

TEST_CASE(split_empty)
{
    auto v = split("", ',');
    CHECK(v.empty());
}

TEST_CASE(trim_basic)
{
    CHECK_EQ(trim("  x  "), "x");
    CHECK_EQ(trim("x"), "x");
    CHECK_EQ(trim("  "), "");
}
