#include "presets/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mfx {

namespace {
struct Parser {
    const std::string& t;
    size_t i = 0;
    std::string err;
    explicit Parser(const std::string& s) : t(s) {}
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i; }
    bool fail(const char* m) { if (err.empty()) { err = m; err += " at offset " + std::to_string(i); } return false; }
    bool lit(const char* s) { size_t n = std::char_traits<char>::length(s); if (t.compare(i, n, s) == 0) { i += n; return true; } return false; }
    bool str(std::string& out) {
        if (t[i] != '"') return fail("expected string");
        ++i;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c == '\\') {
                if (i >= t.size()) return fail("bad escape");
                char e = t[i++];
                switch (e) {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case 'u': {
                        if (i + 4 > t.size()) return fail("bad unicode escape");
                        unsigned cp = (unsigned)std::strtoul(t.substr(i, 4).c_str(), nullptr, 16);
                        i += 4;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                        break;
                    }
                    default: out += e;
                }
            } else out += c;
        }
        if (i >= t.size()) return fail("unterminated string");
        ++i;
        return true;
    }
    bool value(Json& v, int depth) {
        if (depth > 64) return fail("nesting too deep");
        ws();
        if (i >= t.size()) return fail("unexpected end");
        const char c = t[i];
        if (c == '{') {
            ++i; v = Json::object(); ws();
            if (i < t.size() && t[i] == '}') { ++i; return true; }
            while (true) {
                ws(); std::string k;
                if (!str(k)) return false;
                ws(); if (i >= t.size() || t[i] != ':') return fail("expected ':'");
                ++i;
                Json child;
                if (!value(child, depth + 1)) return false;
                v[k] = std::move(child);
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i; v = Json::array(); ws();
            if (i < t.size() && t[i] == ']') { ++i; return true; }
            while (true) {
                Json child;
                if (!value(child, depth + 1)) return false;
                v.push(std::move(child));
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { std::string s; if (!str(s)) return false; v = Json(s); return true; }
        if (lit("true")) { v = Json(true); return true; }
        if (lit("false")) { v = Json(false); return true; }
        if (lit("null")) { v = Json(); return true; }
        const char* start = t.c_str() + i;
        char* end = nullptr;
        const double d = std::strtod(start, &end);
        if (end == start) return fail("unexpected character");
        i += (size_t)(end - start);
        v = Json(d);
        return true;
    }
};

void escape(std::string& s, const std::string& in) {
    s += '"';
    for (char c : in) {
        switch (c) {
            case '"': s += "\\\""; break; case '\\': s += "\\\\"; break;
            case '\n': s += "\\n"; break; case '\t': s += "\\t"; break; case '\r': s += "\\r"; break;
            default:
                if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); s += b; }
                else s += c;
        }
    }
    s += '"';
}
} // namespace

bool Json::parse(const std::string& text, Json& out, std::string* error) {
    Parser p(text);
    if (!p.value(out, 0)) { if (error) *error = p.err; return false; }
    p.ws();
    if (p.i != text.size()) { if (error) *error = "trailing characters"; return false; }
    return true;
}

bool Json::load(const std::string& path, Json& out, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return parse(ss.str(), out, error);
}

bool Json::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    f << dump(2) << "\n";
    return (bool)f;
}

std::string Json::dump(int indent) const { std::string s; dumpTo(s, indent, 0); return s; }

void Json::dumpTo(std::string& s, int indent, int level) const {
    const std::string pad = indent > 0 ? std::string((size_t)(indent * (level + 1)), ' ') : "";
    const std::string padEnd = indent > 0 ? std::string((size_t)(indent * level), ' ') : "";
    const char* nl = indent > 0 ? "\n" : "";
    switch (type_) {
        case Type::Null: s += "null"; break;
        case Type::Bool: s += b_ ? "true" : "false"; break;
        case Type::Number: {
            if (!std::isfinite(n_)) { s += "null"; break; }
            char b[32];
            if (n_ == std::floor(n_) && std::fabs(n_) < 1e15) std::snprintf(b, sizeof(b), "%.0f", n_);
            else std::snprintf(b, sizeof(b), "%.9g", n_);
            s += b; break;
        }
        case Type::String: escape(s, s_); break;
        case Type::Array:
            if (arr_.empty()) { s += "[]"; break; }
            s += "["; s += nl;
            for (size_t k = 0; k < arr_.size(); ++k) {
                s += pad; arr_[k].dumpTo(s, indent, level + 1);
                if (k + 1 < arr_.size()) s += ",";
                s += nl;
            }
            s += padEnd; s += "]"; break;
        case Type::Object:
            if (obj_.empty()) { s += "{}"; break; }
            s += "{"; s += nl;
            {
                size_t k = 0;
                for (const auto& [key, v] : obj_) {
                    s += pad; escape(s, key); s += indent > 0 ? ": " : ":";
                    v.dumpTo(s, indent, level + 1);
                    if (++k < obj_.size()) s += ",";
                    s += nl;
                }
            }
            s += padEnd; s += "}"; break;
    }
}

} // namespace mfx
