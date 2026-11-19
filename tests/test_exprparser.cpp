#include "test_framework.h"
#include "util/ExprParser.h"

TEST_CASE(expr_arithmetic)
{
    ExprValue v;
    ExprParser p("1+2*3");
    CHECK(p.parse(v));
    CHECK(!v.isBool);
    CHECK_EQ(v.intVal, 7);
}

TEST_CASE(expr_comparison_true)
{
    ExprValue v;
    ExprParser p("5>3");
    CHECK(p.parse(v));
    CHECK(v.isBool);
    CHECK(v.boolVal);
}

TEST_CASE(expr_comparison_false)
{
    ExprValue v;
    ExprParser p("2<1");
    CHECK(p.parse(v));
    CHECK(v.isBool);
    CHECK(!v.boolVal);
}

TEST_CASE(expr_equality)
{
    ExprValue v;
    ExprParser p("1+2==3");
    CHECK(p.parse(v));
    CHECK(v.isBool);
    CHECK(v.boolVal);
}

TEST_CASE(expr_paren)
{
    ExprValue v;
    ExprParser p("(1+2)*3");
    CHECK(p.parse(v));
    CHECK_EQ(v.intVal, 9);
}

TEST_CASE(expr_modulo)
{
    ExprValue v;
    ExprParser p("10%3");
    CHECK(p.parse(v));
    CHECK_EQ(v.intVal, 1);
}

TEST_CASE(expr_invalid_trailing_op)
{
    ExprValue v;
    ExprParser p("1+");
    CHECK(!p.parse(v));
}

TEST_CASE(expr_division_by_zero)
{
    ExprValue v;
    ExprParser p("1/0");
    CHECK(!p.parse(v));
}
