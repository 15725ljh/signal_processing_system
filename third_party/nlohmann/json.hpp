// ============================================================================
//  mini_json.hpp — 极简 JSON 解析器 (替代 nlohmann/json.hpp 24765 行)
//
//  仅实现本项目 Config.h 所需的 API 子集:
//    nlohmann::json::parse(str)       解析 JSON 字符串
//    nlohmann::json::parse_error      解析异常
//    .is_null() / .is_object() / .is_number()
//    .contains(key) / operator[](key)
//    .get<double>() / .get<int>()
//
//  不支持: 数组下标访问、序列化、Schema 验证等
// ============================================================================
#ifndef MINI_JSON_HPP_GUARD
#define MINI_JSON_HPP_GUARD

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace nlohmann {

// ── 异常类型 ──
class parse_error : public std::exception {
    std::string msg_;
public:
    explicit parse_error(std::string msg) : msg_(std::move(msg)) {}
    const char* what() const noexcept override { return msg_.c_str(); }
};

// ── JSON 值 ──
class json {
public:
    enum class value_t { null, object, number_integer, number_float, string, boolean };

    // 允许 nlohmann::json::parse_error 访问(兼容 Config.h 用法)
    using parse_error = nlohmann::parse_error;

private:
    value_t type_ = value_t::null;
    std::map<std::string, json> object_;
    double number_ = 0.0;
    std::string str_;

public:
    json() = default;
    json(std::nullptr_t) : type_(value_t::null) {}

    static json object() { json j; j.type_ = value_t::object; return j; }

    bool is_null()   const { return type_ == value_t::null; }
    bool is_object() const { return type_ == value_t::object; }
    bool is_number() const { return type_ == value_t::number_integer || type_ == value_t::number_float; }
    bool is_string() const { return type_ == value_t::string; }
    bool is_boolean() const { return type_ == value_t::boolean; }

    bool contains(const std::string& key) const {
        return is_object() && object_.find(key) != object_.end();
    }
    const json& operator[](const std::string& key) const {
        static json null_val;
        auto it = object_.find(key);
        return (it != object_.end()) ? it->second : null_val;
    }
    json& operator[](const std::string& key) {
        if (type_ == value_t::null) type_ = value_t::object;
        return object_[key];
    }

    template<typename T> T get() const;

    static json parse(const std::string& str) {
        size_t pos = 0;
        json result = parseValue(str, pos);
        skipWS(str, pos);
        if (pos < str.size())
            throw parse_error("unexpected trailing characters at position " + std::to_string(pos));
        return result;
    }

private:
    static void skipWS(const std::string& s, size_t& pos) {
        while (pos < s.size() && (s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r'))
            ++pos;
    }

    static json parseValue(const std::string& s, size_t& pos) {
        skipWS(s, pos);
        if (pos >= s.size()) throw parse_error("unexpected end of input");
        char c = s[pos];
        if (c == '{') return parseObject(s, pos);
        if (c == '[') return parseArray(s, pos);
        if (c == '"') return parseString(s, pos);
        if (c == 't' || c == 'f') return parseBool(s, pos);
        if (c == 'n') return parseNull(s, pos);
        return parseNumber(s, pos);
    }

    static json parseObject(const std::string& s, size_t& pos) {
        json result = json::object();
        ++pos;
        skipWS(s, pos);
        if (pos < s.size() && s[pos] == '}') { ++pos; return result; }
        while (true) {
            skipWS(s, pos);
            if (pos >= s.size()) throw parse_error("unexpected end in object");
            if (s[pos] != '"') throw parse_error("expected string key at " + std::to_string(pos));
            json key_j = parseString(s, pos);
            skipWS(s, pos);
            if (pos >= s.size() || s[pos] != ':')
                throw parse_error("expected ':' at " + std::to_string(pos));
            ++pos;
            json val = parseValue(s, pos);
            result.object_[key_j.str_] = std::move(val);
            skipWS(s, pos);
            if (pos >= s.size()) throw parse_error("unexpected end in object");
            if (s[pos] == '}') { ++pos; return result; }
            if (s[pos] == ',') { ++pos; skipWS(s, pos); continue; }
            throw parse_error("expected ',' or '}' at " + std::to_string(pos));
        }
    }

    static json parseArray(const std::string& s, size_t& pos) {
        json result = json::object();
        ++pos;
        skipWS(s, pos);
        if (pos < s.size() && s[pos] == ']') { ++pos; return result; }
        int idx = 0;
        while (true) {
            json val = parseValue(s, pos);
            result.object_[std::to_string(idx++)] = std::move(val);
            skipWS(s, pos);
            if (pos >= s.size()) throw parse_error("unexpected end in array");
            if (s[pos] == ']') { ++pos; return result; }
            if (s[pos] == ',') { ++pos; skipWS(s, pos); continue; }
            throw parse_error("expected ',' or ']' at " + std::to_string(pos));
        }
    }

    static json parseString(const std::string& s, size_t& pos) {
        ++pos;
        std::string result;
        while (pos < s.size()) {
            if (s[pos] == '\\') {
                ++pos;
                if (pos >= s.size()) throw parse_error("unterminated string escape");
                switch (s[pos]) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'n':  result += '\n'; break;
                    case 't':  result += '\t'; break;
                    case 'r':  result += '\r'; break;
                    default:   result += s[pos]; break;
                }
                ++pos;
            } else if (s[pos] == '"') {
                ++pos;
                json j;
                j.type_ = value_t::string;
                j.str_ = std::move(result);
                return j;
            } else {
                result += s[pos++];
            }
        }
        throw parse_error("unterminated string");
    }

    static json parseNumber(const std::string& s, size_t& pos) {
        size_t start = pos;
        if (pos < s.size() && s[pos] == '-') ++pos;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
        }
        std::string num_str = s.substr(start, pos - start);
        json j;
        j.number_ = std::strtod(num_str.c_str(), nullptr);
        j.type_ = value_t::number_float;
        return j;
    }

    static json parseBool(const std::string& s, size_t& pos) {
        json j;
        j.type_ = value_t::boolean;
        if (s.compare(pos, 4, "true") == 0)  { j.number_ = 1.0; pos += 4; return j; }
        if (s.compare(pos, 5, "false") == 0)  { j.number_ = 0.0; pos += 5; return j; }
        throw parse_error("invalid literal at " + std::to_string(pos));
    }

    static json parseNull(const std::string& s, size_t& pos) {
        if (s.compare(pos, 4, "null") == 0) { pos += 4; return json(); }
        throw parse_error("invalid literal at " + std::to_string(pos));
    }
};

template<> inline double json::get<double>() const { return number_; }
template<> inline int    json::get<int>()    const { return static_cast<int>(number_); }
template<> inline float  json::get<float>()  const { return static_cast<float>(number_); }
template<> inline bool   json::get<bool>()   const { return number_ != 0.0; }
template<> inline std::string json::get<std::string>() const { return str_; }

} // namespace nlohmann

#endif // MINI_JSON_HPP_GUARD
