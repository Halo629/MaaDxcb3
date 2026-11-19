#pragma once

#include <cctype>
#include <cstring>
#include <string>
#include <utility>

// 表达式求值结果：整数或布尔，最终表达式必须是布尔。
struct ExprValue
{
    bool isBool = false;
    long long intVal = 0;
    bool boolVal = false;
};

// 递归下降表达式求值器：算术 +-* /%，比较 < <= > >= == !=，逻辑 && || !，分组 ()，整数。
class ExprParser
{
public:
    explicit ExprParser(std::string s)
        : s_(std::move(s))
    {
    }

    bool parse(ExprValue& out)
    {
        bool ok = parseOr(out);
        skipSpace();
        return ok && pos_ == s_.size();
    }

private:
    std::string s_;
    size_t pos_ = 0;

    void skipSpace()
    {
        while (pos_ < s_.size() && std::isspace((unsigned char)s_[pos_])) {
            ++pos_;
        }
    }

    bool match(const char* op)
    {
        size_t len = std::strlen(op);
        if (pos_ + len <= s_.size() && s_.compare(pos_, len, op) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    bool consume(char c)
    {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool parseOr(ExprValue& out)
    {
        if (!parseAnd(out)) {
            return false;
        }
        skipSpace();
        while (match("||")) {
            skipSpace();
            ExprValue rhs;
            if (!parseAnd(rhs) || !out.isBool || !rhs.isBool) {
                return false;
            }
            out.boolVal = out.boolVal || rhs.boolVal;
            skipSpace();
        }
        return true;
    }

    bool parseAnd(ExprValue& out)
    {
        if (!parseEquality(out)) {
            return false;
        }
        skipSpace();
        while (match("&&")) {
            skipSpace();
            ExprValue rhs;
            if (!parseEquality(rhs) || !out.isBool || !rhs.isBool) {
                return false;
            }
            out.boolVal = out.boolVal && rhs.boolVal;
            skipSpace();
        }
        return true;
    }

    bool parseEquality(ExprValue& out)
    {
        if (!parseRelational(out)) {
            return false;
        }
        skipSpace();
        while (true) {
            bool negate = false;
            if (match("==")) {
                negate = false;
            }
            else if (match("!=")) {
                negate = true;
            }
            else {
                break;
            }
            skipSpace();
            ExprValue rhs;
            if (!parseRelational(rhs) || !equal(out, rhs)) {
                return false;
            }
            if (negate) {
                out.boolVal = !out.boolVal;
            }
            skipSpace();
        }
        return true;
    }

    bool parseRelational(ExprValue& out)
    {
        if (!parseAdditive(out)) {
            return false;
        }
        skipSpace();
        while (true) {
            int op = 0;
            if (match("<=")) {
                op = 1;
            }
            else if (match(">=")) {
                op = 2;
            }
            else if (match("<")) {
                op = 3;
            }
            else if (match(">")) {
                op = 4;
            }
            else {
                break;
            }
            skipSpace();
            ExprValue rhs;
            if (!parseAdditive(rhs) || !relational(out, rhs, op)) {
                return false;
            }
            skipSpace();
        }
        return true;
    }

    bool parseAdditive(ExprValue& out)
    {
        if (!parseMultiplicative(out)) {
            return false;
        }
        skipSpace();
        while (true) {
            char op = 0;
            if (consume('+')) {
                op = '+';
            }
            else if (consume('-')) {
                op = '-';
            }
            else {
                break;
            }
            skipSpace();
            ExprValue rhs;
            if (!parseMultiplicative(rhs) || !arith(out, rhs, op)) {
                return false;
            }
            skipSpace();
        }
        return true;
    }

    bool parseMultiplicative(ExprValue& out)
    {
        if (!parseUnary(out)) {
            return false;
        }
        skipSpace();
        while (true) {
            char op = 0;
            if (consume('*')) {
                op = '*';
            }
            else if (consume('/')) {
                op = '/';
            }
            else if (consume('%')) {
                op = '%';
            }
            else {
                break;
            }
            skipSpace();
            ExprValue rhs;
            if (!parseUnary(rhs) || !arith(out, rhs, op)) {
                return false;
            }
            skipSpace();
        }
        return true;
    }

    bool parseUnary(ExprValue& out)
    {
        skipSpace();
        if (consume('!')) {
            if (!parseUnary(out) || !out.isBool) {
                return false;
            }
            out.boolVal = !out.boolVal;
            return true;
        }
        if (consume('-')) {
            if (!parseUnary(out) || out.isBool) {
                return false;
            }
            out.intVal = -out.intVal;
            return true;
        }
        if (consume('+')) {
            return parseUnary(out);
        }
        return parsePrimary(out);
    }

    bool parsePrimary(ExprValue& out)
    {
        skipSpace();
        if (consume('(')) {
            if (!parseOr(out)) {
                return false;
            }
            skipSpace();
            return consume(')');
        }
        size_t start = pos_;
        while (pos_ < s_.size() && std::isdigit((unsigned char)s_[pos_])) {
            ++pos_;
        }
        if (start == pos_) {
            return false;
        }
        out.isBool = false;
        out.intVal = std::stoll(s_.substr(start, pos_ - start));
        return true;
    }

    static bool arith(ExprValue& l, const ExprValue& r, char op)
    {
        if (l.isBool || r.isBool) {
            return false;
        }
        switch (op) {
        case '+':
            l.intVal += r.intVal;
            return true;
        case '-':
            l.intVal -= r.intVal;
            return true;
        case '*':
            l.intVal *= r.intVal;
            return true;
        case '/':
            if (r.intVal == 0) {
                return false;
            }
            l.intVal /= r.intVal;
            return true;
        case '%':
            if (r.intVal == 0) {
                return false;
            }
            l.intVal %= r.intVal;
            return true;
        default:
            return false;
        }
    }

    static bool relational(ExprValue& l, const ExprValue& r, int op)
    {
        if (l.isBool || r.isBool) {
            return false;
        }
        bool result = false;
        switch (op) {
        case 1:
            result = l.intVal <= r.intVal;
            break;
        case 2:
            result = l.intVal >= r.intVal;
            break;
        case 3:
            result = l.intVal < r.intVal;
            break;
        case 4:
            result = l.intVal > r.intVal;
            break;
        default:
            return false;
        }
        l.isBool = true;
        l.boolVal = result;
        return true;
    }

    static bool equal(ExprValue& l, const ExprValue& r)
    {
        if (l.isBool != r.isBool) {
            return false;
        }
        bool result = l.isBool ? (l.boolVal == r.boolVal) : (l.intVal == r.intVal);
        l.isBool = true;
        l.boolVal = result;
        return true;
    }
};
