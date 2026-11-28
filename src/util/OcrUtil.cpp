#include "OcrUtil.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ocr {

namespace {

void appendText(const json::value& obj, const std::string& text, std::vector<Text>& out)
{
    if (text.empty()) {
        return;
    }
    Text item;
    item.text = text;
    if (obj.is_object()) {
        const auto& box = obj.at("box");
        if (box.is_array() && box.as_array().size() >= 4) {
            const auto& arr = box.as_array();
            item.box = { arr[0].as_integer(), arr[1].as_integer(), arr[2].as_integer(), arr[3].as_integer() };
        }
    }
    out.push_back(std::move(item));
}

void collectRecursive(const json::value& v, std::vector<Text>& out)
{
    if (v.is_array()) {
        for (const auto& child : v.as_array()) {
            collectRecursive(child, out);
        }
        return;
    }
    if (!v.is_object()) {
        return;
    }

    const auto& obj = v.as_object();
    const size_t before = out.size();

    for (const char* key : { "all", "filtered" }) {
        if (!obj.contains(key)) {
            continue;
        }
        const auto& arr = obj.at(key);
        if (!arr.is_array()) {
            continue;
        }
        for (const auto& item : arr.as_array()) {
            if (item.is_object() && item.as_object().contains("text")) {
                const auto& t = item.as_object().at("text");
                if (t.is_string()) {
                    appendText(item, t.as_string(), out);
                }
            }
        }
    }

    if (out.size() == before && obj.contains("best")) {
        const auto& best = obj.at("best");
        if (best.is_object() && best.as_object().contains("text")) {
            const auto& t = best.as_object().at("text");
            if (t.is_string()) {
                appendText(best, t.as_string(), out);
            }
        }
    }

    for (const auto& [key, child] : obj) {
        collectRecursive(child, out);
    }
}

std::string normalizeNumberToken(std::string token)
{
    token.erase(std::remove_if(token.begin(), token.end(), [](char c) { return std::isspace((unsigned char)c); }),
                token.end());

    if (token.find('.') != std::string::npos) {
        token.erase(std::remove(token.begin(), token.end(), ','), token.end());
        return token;
    }

    const size_t commaCount = std::count(token.begin(), token.end(), ',');
    if (commaCount == 1) {
        const size_t pos = token.find(',');
        if (token.size() - pos - 1 != 3) {
            token[pos] = '.';
        }
        else {
            token.erase(pos, 1);
        }
    }
    else {
        token.erase(std::remove(token.begin(), token.end(), ','), token.end());
    }
    return token;
}

} // namespace

std::vector<Text> collectTexts(const json::value& detail)
{
    std::vector<Text> out;
    collectRecursive(detail, out);
    return out;
}

bool extractNumber(const std::vector<Text>& texts, int& outValue)
{
    for (const auto& item : texts) {
        std::string digits;
        for (char c : item.text) {
            if (c >= '0' && c <= '9') {
                digits += c;
            }
            else if (!digits.empty()) {
                break;
            }
        }
        if (!digits.empty()) {
            outValue = std::atoi(digits.c_str());
            return true;
        }
    }
    return false;
}

bool parseNumeric(const std::string& text, double& outValue)
{
    std::string cleaned;
    for (char c : text) {
        if (!std::isspace((unsigned char)c)) {
            cleaned += c;
        }
    }
    if (cleaned.empty()) {
        return false;
    }

    size_t i = 0;
    while (i < cleaned.size()) {
        const char c = cleaned[i];
        if (std::isdigit((unsigned char)c) || c == '.' || c == ',' || c == '+' || c == '-') {
            break;
        }
        ++i;
    }
    if (i >= cleaned.size()) {
        return false;
    }

    const size_t start = i;
    while (i < cleaned.size()) {
        const char c = cleaned[i];
        if (std::isdigit((unsigned char)c) || c == '.' || c == ',' || c == '+' || c == '-') {
            ++i;
            continue;
        }
        break;
    }
    const std::string token = cleaned.substr(start, i - start);

    double multiplier = 1.0;
    size_t j = i;
    while (j < cleaned.size() && std::isspace((unsigned char)cleaned[j])) {
        ++j;
    }
    if (j < cleaned.size()) {
        const std::string suffix = cleaned.substr(j, 3);
        if (suffix.rfind("万", 0) == 0) {
            multiplier = 10000.0;
        }
        else if (suffix.rfind("亿", 0) == 0) {
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

    const std::string normalized = normalizeNumberToken(token);
    if (normalized.empty()) {
        return false;
    }
    const double value = std::stod(normalized);
    outValue = value * multiplier;
    return true;
}

} // namespace ocr
