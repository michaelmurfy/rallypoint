// engine/mp/mini_json.cpp -- see mini_json.hpp.
#include "mp/mini_json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ra2mp {

namespace {
const Json kNull;

struct Parser {
    const std::string& t;
    std::size_t p = 0;
    int depth = 0;

    void ws() {
        while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\r' || t[p] == '\n')) ++p;
    }
    bool lit(const char* w) {
        std::size_t k = 0;
        while (w[k]) {
            if (p + k >= t.size() || t[p + k] != w[k]) return false;
            ++k;
        }
        p += k;
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) {
            o += static_cast<char>(0xC0 | (cp >> 6));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += static_cast<char>(0xE0 | (cp >> 12));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            o += static_cast<char>(0xF0 | (cp >> 18));
            o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            o += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (p + 4 > t.size()) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& o) {
        if (p >= t.size() || t[p] != '"') return false;
        ++p;
        while (p < t.size()) {
            const char c = t[p++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') { o += c; continue; }
            if (p >= t.size()) return false;
            const char e = t[p++];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= t.size() && t[p] == '\\' && t[p + 1] == 'u') {
                        p += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(Json& v) {
        if (++depth > 16) return false;
        ws();
        if (p >= t.size()) return false;
        const char c = t[p];
        bool ok = true;
        if (c == '{') {
            ++p;
            v.kind = Json::Object;
            ws();
            if (p < t.size() && t[p] == '}') { ++p; --depth; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (p >= t.size() || t[p] != ':') return false;
                ++p;
                Json child;
                if (!value(child)) return false;
                v.obj[k] = std::move(child);
                ws();
                if (p < t.size() && t[p] == ',') { ++p; continue; }
                if (p < t.size() && t[p] == '}') { ++p; break; }
                return false;
            }
        } else if (c == '[') {
            ++p;
            v.kind = Json::Array;
            ws();
            if (p < t.size() && t[p] == ']') { ++p; --depth; return true; }
            for (;;) {
                Json child;
                if (!value(child)) return false;
                v.arr.push_back(std::move(child));
                ws();
                if (p < t.size() && t[p] == ',') { ++p; continue; }
                if (p < t.size() && t[p] == ']') { ++p; break; }
                return false;
            }
        } else if (c == '"') {
            v.kind = Json::String;
            ok = string(v.str);
        } else if (lit("true")) {
            v.kind = Json::Bool;
            v.b = true;
        } else if (lit("false")) {
            v.kind = Json::Bool;
        } else if (lit("null")) {
            v.kind = Json::Null;
        } else {
            const char* start = t.c_str() + p;
            char* end = nullptr;
            v.num = std::strtod(start, &end);
            if (end == start) return false;
            v.kind = Json::Number;
            p += static_cast<std::size_t>(end - start);
        }
        --depth;
        return ok;
    }
};
}  // namespace

const Json& Json::operator[](const std::string& k) const {
    if (kind != Object) return kNull;
    const auto it = obj.find(k);
    return it == obj.end() ? kNull : it->second;
}

std::string Json::s(const std::string& k, const std::string& def) const {
    const Json& v = (*this)[k];
    return v.kind == String ? v.str : def;
}

double Json::n(const std::string& k, double def) const {
    const Json& v = (*this)[k];
    return v.kind == Number ? v.num : def;
}

std::int64_t Json::i(const std::string& k, std::int64_t def) const {
    const Json& v = (*this)[k];
    return v.kind == Number ? static_cast<std::int64_t>(std::llround(v.num)) : def;
}

std::uint32_t Json::u32(const std::string& k) const {
    const std::int64_t v = i(k, 0);
    return (v < 0 || v > 0xFFFFFFFFll) ? 0u : static_cast<std::uint32_t>(v);
}

bool Json::flag(const std::string& k) const {
    const Json& v = (*this)[k];
    return v.kind == Bool && v.b;
}

bool json_parse(const std::string& text, Json& out) {
    out = Json{};
    Parser ps{text};
    Json v;
    if (!ps.value(v)) return false;
    ps.ws();
    if (ps.p != text.size()) return false;
    out = std::move(v);
    return true;
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

void JsonWriter::key(const std::string& k) {
    if (!body_.empty()) body_ += ',';
    body_ += '"' + json_escape(k) + "\":";
}

JsonWriter& JsonWriter::str(const std::string& k, const std::string& v) {
    key(k);
    body_ += '"' + json_escape(v) + '"';
    return *this;
}

JsonWriter& JsonWriter::num(const std::string& k, double v) {
    key(k);
    char b[32];
    if (v == std::floor(v) && std::fabs(v) < 9e15) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
    else std::snprintf(b, sizeof b, "%.17g", v);
    body_ += b;
    return *this;
}

JsonWriter& JsonWriter::u64(const std::string& k, std::uint64_t v) {
    key(k);
    body_ += std::to_string(v);
    return *this;
}

JsonWriter& JsonWriter::boolean(const std::string& k, bool v) {
    key(k);
    body_ += v ? "true" : "false";
    return *this;
}

}  // namespace ra2mp
