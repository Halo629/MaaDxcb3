#include "test_framework.h"
#include "util/OcrUtil.h"

TEST_CASE(parse_numeric_plain)
{
    double v = 0;
    CHECK(ocr::parseNumeric("12345", v));
    CHECK_EQ(v, 12345.0);
}

TEST_CASE(parse_numeric_decimal)
{
    double v = 0;
    CHECK(ocr::parseNumeric("89.1", v));
    CHECK_EQ(v, 89.1);
}

TEST_CASE(parse_numeric_wan)
{
    double v = 0;
    CHECK(ocr::parseNumeric("1.2万", v));
    CHECK_EQ(v, 12000.0);
}

TEST_CASE(parse_numeric_yi)
{
    double v = 0;
    CHECK(ocr::parseNumeric("3亿", v));
    CHECK_EQ(v, 300000000.0);
}

TEST_CASE(parse_numeric_suffix_k)
{
    double v = 0;
    CHECK(ocr::parseNumeric("5K", v));
    CHECK_EQ(v, 5000.0);
}

TEST_CASE(parse_numeric_suffix_m)
{
    double v = 0;
    CHECK(ocr::parseNumeric("1.5M", v));
    CHECK_EQ(v, 1500000.0);
}

TEST_CASE(parse_numeric_suffix_b)
{
    double v = 0;
    CHECK(ocr::parseNumeric("2B", v));
    CHECK_EQ(v, 2000000000.0);
}

TEST_CASE(parse_numeric_comma_thousands)
{
    double v = 0;
    CHECK(ocr::parseNumeric("1,234", v));
    CHECK_EQ(v, 1234.0);
}

TEST_CASE(parse_numeric_empty)
{
    double v = 0;
    CHECK(!ocr::parseNumeric("", v));
}

TEST_CASE(parse_numeric_no_digit)
{
    double v = 0;
    CHECK(!ocr::parseNumeric("abc", v));
}

TEST_CASE(extract_number_basic)
{
    std::vector<ocr::Text> texts = { { "战力12345" } };
    int v = 0;
    CHECK(ocr::extractNumber(texts, v));
    CHECK_EQ(v, 12345);
}

TEST_CASE(extract_number_empty)
{
    std::vector<ocr::Text> texts;
    int v = 0;
    CHECK(!ocr::extractNumber(texts, v));
}

TEST_CASE(extract_number_no_digit)
{
    std::vector<ocr::Text> texts = { { "金币" } };
    int v = 0;
    CHECK(!ocr::extractNumber(texts, v));
}
