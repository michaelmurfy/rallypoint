#pragma once
// engine/mp/mini_json -- the tiny JSON reader/writer the ra2lobbyd control
// plane needs (flat objects, arrays of flat objects, strings, numbers,
// booleans). Not a general-purpose library: depth-limited, UTF-8 passed
// through, \uXXXX decoded to UTF-8. SDL-free.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ra2mp {

struct Json {
    enum Kind { Null, Bool, Number, String, Array, Object } kind = Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;

    // Lookups that never throw (missing -> defaults).
    const Json& operator[](const std::string& k) const;
    std::string s(const std::string& k, const std::string& def = "") const;
    double n(const std::string& k, double def = 0.0) const;
    std::int64_t i(const std::string& k, std::int64_t def = 0) const;
    std::uint32_t u32(const std::string& k) const;
    bool flag(const std::string& k) const;
    bool has(const std::string& k) const { return kind == Object && obj.count(k) != 0; }
};

// Parse one JSON text; false on malformed input (out left Null).
bool json_parse(const std::string& text, Json& out);

// Writer for one flat object: JsonWriter w; w.str("t","hello").num("proto",1); w.done()
class JsonWriter {
public:
    JsonWriter& str(const std::string& k, const std::string& v);
    JsonWriter& num(const std::string& k, double v);
    JsonWriter& u64(const std::string& k, std::uint64_t v);
    JsonWriter& boolean(const std::string& k, bool v);
    std::string done() const { return "{" + body_ + "}"; }

private:
    void key(const std::string& k);
    std::string body_;
};

std::string json_escape(const std::string& s);

}  // namespace ra2mp
