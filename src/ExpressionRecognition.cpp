#include "Comm.h"

#include <cctype>

namespace
{
// ──── 表达式求值 ──────────────────────────────────────────────────
// 求值结果：整数或布尔。表达式最终必须是布尔值。
struct ExprValue
{
    bool isBool = false;
    long long intVal = 0;
    bool boolVal = false;
};

// 递归下降表达式求值器。支持：
//   算术 + - * / %，比较 < <= > >= == !=，逻辑 && || !，分组 ()，整数。
class ExprParser
{
public:
    explicit ExprParser(const std::string& s)
        : s_(s)
    {
    }

    bool parse(ExprValue& out)
    {
        bool ok = parseOr(out);
        skipSpace();
        return ok && pos_ == s_.size();
    }

private:
    const std::string& s_;
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
            int op = 0; // 1 <=, 2 >=, 3 <, 4 >
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

// ──── OCR 数值解析 ───────────────────────────────────────────────
bool startsWith(const std::string& s, size_t pos, const std::string& prefix)
{
    if (pos + prefix.size() > s.size()) {
        return false;
    }
    return s.compare(pos, prefix.size(), prefix) == 0;
}

// 归一化 OCR 数值 token：处理逗号/小数点。
std::string normalizeNumberToken(const std::string& token)
{
    std::string s = token;
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return std::isspace((unsigned char)c); }), s.end());

    if (s.find('.') != std::string::npos) {
        s.erase(std::remove(s.begin(), s.end(), ','), s.end());
    }
    else {
        size_t commaCount = std::count(s.begin(), s.end(), ',');
        if (commaCount == 1) {
            size_t pos = s.find(',');
            if (s.size() - pos - 1 != 3) {
                s[pos] = '.';
            }
            else {
                s.erase(pos, 1);
            }
        }
        else {
            s.erase(std::remove(s.begin(), s.end(), ','), s.end());
        }
    }
    return s;
}

// 解析 OCR 文本中的数值，支持 万/亿/K/M/B 缩写。
bool parseOcrNumericValue(const std::string& text, long long& outValue)
{
    std::string cleaned = trim(text);
    if (cleaned.empty()) {
        return false;
    }

    // 定位第一个数字（含符号/小数点/逗号）
    size_t i = 0;
    while (i < cleaned.size()) {
        char c = cleaned[i];
        if (std::isdigit((unsigned char)c) || c == '.' || c == ',' || c == '+' || c == '-') {
            break;
        }
        ++i;
    }
    if (i >= cleaned.size()) {
        return false;
    }

    size_t start = i;
    while (i < cleaned.size()) {
        char c = cleaned[i];
        if (std::isdigit((unsigned char)c) || c == '.' || c == ',' || c == '+' || c == '-') {
            ++i;
            continue;
        }
        break;
    }
    std::string token = cleaned.substr(start, i - start);

    // 确定乘数后缀
    double multiplier = 1.0;
    size_t j = i;
    while (j < cleaned.size() && std::isspace((unsigned char)cleaned[j])) {
        ++j;
    }
    if (j < cleaned.size()) {
        if (startsWith(cleaned, j, "万")) {
            multiplier = 10000.0;
        }
        else if (startsWith(cleaned, j, "亿")) {
            multiplier = 100000000.0;
        }
        else if (cleaned[j] == 'K' || cleaned[j] == 'k') {
            multiplier = 1000.0;
        }
        else if (cleaned[j] == 'M' || cleaned[j] == 'm') {
            multiplier = 1000000.0;
        }
        else if (cleaned[j] == 'B' || cleaned[j] == 'b') {
            multiplier = 1000000000.0;
        }
    }

    std::string normalized = normalizeNumberToken(token);
    if (normalized.empty()) {
        return false;
    }

    double value = std::stod(normalized);
    outValue = (long long)std::round(value * multiplier);
    return true;
}

// 从 OCR 识别 detail 对象（{all, filtered, best}）中提取数值。
bool extractNumberFromOcrDetail(json::value& detail, long long& outValue)
{
    if (!detail.is_object()) {
        return false;
    }
    if (detail.contains("best") && detail["best"].is_object()) {
        auto& best = detail["best"];
        if (best.contains("text") && best["text"].is_string()) {
            return parseOcrNumericValue(best["text"].as_string(), outValue);
        }
    }
    if (detail.contains("all") && detail["all"].is_array()) {
        for (auto& item : detail["all"].as_array()) {
            if (item.is_object() && item.contains("text") && item["text"].is_string()) {
                return parseOcrNumericValue(item["text"].as_string(), outValue);
            }
        }
    }
    return false;
}

// ──── 节点识别 ────────────────────────────────────────────────────
// 判断节点是否为 And，并读取其 box_index。
bool resolveAndBoxIndex(MaaContext* context, const char* nodeName, bool& isAnd, int& boxIndex)
{
    isAnd = false;
    boxIndex = 0;

    auto buf = MaaStringBufferCreate();
    if (!MaaContextGetNodeData(context, nodeName, buf)) {
        MaaStringBufferDestroy(buf);
        return false;
    }
    std::string raw(MaaStringBufferGet(buf), MaaStringBufferSize(buf));
    MaaStringBufferDestroy(buf);

    auto j = json::parse(raw).value_or(json::value {});
    if (!j.is_object()) {
        return false;
    }
    auto& reco = j["recognition"];
    if (!reco.is_object()) {
        return false;
    }
    auto& type = reco["type"];
    if (!type.is_string() || type.as_string() != "And") {
        return true; // 不是 And，但成功读取了节点数据
    }

    isAnd = true;
    auto& param = reco["param"];
    if (param.is_object() && param.contains("box_index") && param["box_index"].is_number()) {
        boxIndex = (int)param["box_index"].as_integer();
    }
    return true;
}

// 运行指定节点的识别，提取其中的 OCR 数值。
bool runNumericRecognition(MaaContext* context, const MaaImageBuffer* image, const char* nodeName, long long& outValue)
{
    auto* tasker = MaaContextGetTasker(context);
    auto recoId = MaaContextRunRecognition(context, nodeName, "{}", image);
    if (recoId == MaaInvalidId) {
        return false;
    }

    auto detailBuf = MaaStringBufferCreate();
    MaaBool hit = false;
    MaaRect box;
    MaaTaskerGetRecognitionDetail(tasker, recoId, nullptr, nullptr, &hit, &box, detailBuf, nullptr, nullptr);
    std::string detail(MaaStringBufferGet(detailBuf), MaaStringBufferSize(detailBuf));
    MaaStringBufferDestroy(detailBuf);

    if (!hit) {
        return false;
    }

    bool isAnd = false;
    int boxIndex = 0;
    resolveAndBoxIndex(context, nodeName, isAnd, boxIndex);

    auto j = json::parse(detail).value_or(json::value {});

    if (isAnd) {
        if (!j.is_array()) {
            return false;
        }
        auto& arr = j.as_array();
        if (boxIndex < 0 || (size_t)boxIndex >= arr.size() || !arr[boxIndex].is_object()) {
            return false;
        }
        auto& subDetail = arr[boxIndex]["detail"];
        return extractNumberFromOcrDetail(subDetail, outValue);
    }

    return extractNumberFromOcrDetail(j, outValue);
}

// 解析表达式中的 {节点名} 占位符，替换为对应 OCR 数值。
bool resolveExpressionValues(MaaContext* context, const MaaImageBuffer* image, const std::string& expression, std::string& outResolved)
{
    std::string result;
    size_t i = 0;
    while (i < expression.size()) {
        if (expression[i] != '{') {
            result += expression[i];
            ++i;
            continue;
        }
        size_t close = expression.find('}', i + 1);
        if (close == std::string::npos) {
            return false;
        }
        std::string nodeName = trim(expression.substr(i + 1, close - i - 1));
        if (nodeName.empty()) {
            return false;
        }
        long long value = 0;
        if (!runNumericRecognition(context, image, nodeName.c_str(), value)) {
            return false;
        }
        result += std::to_string(value);
        i = close + 1;
    }
    outResolved = std::move(result);
    return true;
}

// 解析 box_node 的结果框。
bool resolveResultBox(MaaContext* context, const MaaImageBuffer* image, const char* boxNode, MaaRect& outBox)
{
    auto* tasker = MaaContextGetTasker(context);
    auto recoId = MaaContextRunRecognition(context, boxNode, "{}", image);
    if (recoId == MaaInvalidId) {
        return false;
    }
    MaaBool hit = false;
    MaaRect box;
    MaaTaskerGetRecognitionDetail(tasker, recoId, nullptr, nullptr, &hit, &box, nullptr, nullptr, nullptr);
    if (!hit) {
        return false;
    }
    outBox = box;
    return true;
}
}

// ──── 自定义识别：ExpressionRecognition ─────────────────────────
// 计算由数字识别节点组成的布尔表达式。
// custom_recognition_param:
//   expression: string，必填，最终必须计算为布尔值。占位符 {节点名} 引用其他识别节点。
//   box_node:   string，可选，命中后返回该节点的结果框；默认返回当前识别 ROI。
MaaBool expressionRecognition(
    MaaContext* context,
    MaaTaskId taskId,
    const char* nodeName,
    const char* customRecognitionName,
    const char* customRecognitionParam,
    const MaaImageBuffer* image,
    const MaaRect* roi,
    void* transArg,
    MaaRect* outBox,
    MaaStringBuffer* outDetail)
{
    auto param = json::parse(customRecognitionParam ? customRecognitionParam : "{}").value_or(json::value {});
    std::string expression = param.is_object() ? param.get("expression", json::value("")).as_string() : "";
    std::string boxNode = param.is_object() ? param.get("box_node", json::value("")).as_string() : "";
    if (expression.empty()) {
        LogUtils::error("ExpressionRecognition: expression 缺失");
        return false;
    }

    std::string resolved;
    if (!resolveExpressionValues(context, image, expression, resolved)) {
        LogUtils::debug(fmt("ExpressionRecognition: 解析表达式值失败: %1", expression));
        return false;
    }

    ExprValue result;
    ExprParser parser(resolved);
    if (!parser.parse(result)) {
        LogUtils::error(fmt("ExpressionRecognition: 表达式求值失败: %1", resolved));
        return false;
    }
    if (!result.isBool) {
        LogUtils::error(fmt("ExpressionRecognition: 表达式结果必须是布尔值: %1", resolved));
        return false;
    }

    bool matched = result.boolVal;
    LogUtils::debug(fmt("ExpressionRecognition: %1 -> %2 (命中=%3)", expression, resolved, matched));

    if (!matched) {
        return false;
    }

    MaaRect resultBox;
    if (boxNode.empty()) {
        resultBox = *roi;
    }
    else if (!resolveResultBox(context, image, boxNode.c_str(), resultBox)) {
        LogUtils::error(fmt("ExpressionRecognition: 解析 box_node %1 结果框失败", boxNode));
        return false;
    }

    if (outBox) {
        *outBox = resultBox;
    }
    if (outDetail) {
        MaaStringBufferSet(outDetail, resolved.c_str());
    }
    return true;
}

void registerCustomExpressionRecognition(MaaResource* res, void* userData)
{
    registerCustomRecognition(res, "ExpressionRecognition", expressionRecognition, userData);
}
