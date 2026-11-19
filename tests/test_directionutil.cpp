#include "test_framework.h"
#include "util/DirectionUtil.h"

TEST_CASE(direction_parse_north)
{
    CHECK(direction::parseDirection("正北") == CompassDir::North);
}

TEST_CASE(direction_parse_north_by_east)
{
    CHECK(direction::parseDirection("北偏东") == CompassDir::NorthByEast);
}

TEST_CASE(direction_parse_single_char)
{
    CHECK(direction::parseDirection("西") == CompassDir::West);
}

TEST_CASE(direction_parse_unknown)
{
    CHECK(direction::parseDirection("随便") == CompassDir::Unknown);
}

TEST_CASE(direction_vector_north)
{
    CHECK(direction::vectorToDirection(0, 1) == CompassDir::North);
}

TEST_CASE(direction_vector_east)
{
    CHECK(direction::vectorToDirection(1, 0) == CompassDir::East);
}

TEST_CASE(direction_vector_zero)
{
    CHECK(direction::vectorToDirection(0, 0) == CompassDir::Unknown);
}

TEST_CASE(coord_basic)
{
    int x = 0, y = 0;
    CHECK(direction::parseCoord("(19,73)", x, y));
    CHECK_EQ(x, 19);
    CHECK_EQ(y, 73);
}

TEST_CASE(coord_with_space)
{
    int x = 0, y = 0;
    CHECK(direction::parseCoord("(19, 73)", x, y));
    CHECK_EQ(x, 19);
    CHECK_EQ(y, 73);
}

TEST_CASE(coord_invalid)
{
    int x = 0, y = 0;
    CHECK(!direction::parseCoord("abc", x, y));
}
