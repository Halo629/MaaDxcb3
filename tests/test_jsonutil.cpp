#include "test_framework.h"
#include "util/JsonUtil.h"

TEST_CASE(parse_valid)
{
    auto j = jsonutil::parse("{\"a\":1}");
    CHECK(j.is_object());
    CHECK_EQ(j["a"].as_integer(), 1);
}

TEST_CASE(parse_invalid)
{
    auto j = jsonutil::parse("not json");
    CHECK(!j.is_object());
    CHECK(!j.is_array());
}

TEST_CASE(read_int_array_full)
{
    auto arr = jsonutil::parse("[10,20]");
    auto v = jsonutil::readIntArray(arr, { 0, 0, 0, 0 });
    CHECK(v.size() == 4);
    CHECK_EQ(v[0], 10);
    CHECK_EQ(v[1], 20);
    CHECK_EQ(v[2], 0);
    CHECK_EQ(v[3], 0);
}

TEST_CASE(read_int_array_not_array)
{
    auto v = jsonutil::readIntArray(json::value {}, { 1, 2, 3 });
    CHECK(v.size() == 3);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 2);
}
