// Small self-contained JSON value / parser / writer (no external dependency, MIT-clean).
// Sufficient for presets, parameter files and benchmark reports. Not for the audio thread.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mfx {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), b_(b) {}
    Json(double d) : type_(Type::Number), n_(d) {}
    Json(int i) : type_(Type::Number), n_(i) {}
    Json(float f) : type_(Type::Number), n_(f) {}
    Json(const char* s) : type_(Type::String), s_(s) {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isBool() const { return type_ == Type::Bool; }

    double asNumber(double def = 0.0) const { return type_ == Type::Number ? n_ : (type_ == Type::Bool ? (b_ ? 1.0 : 0.0) : def); }
    bool asBool(bool def = false) const { return type_ == Type::Bool ? b_ : (type_ == Type::Number ? n_ != 0.0 : def); }
    const std::string& asString() const { static const std::string e; return type_ == Type::String ? s_ : e; }

    // object
    Json& operator[](const std::string& k) { if (type_ != Type::Object) { *this = object(); } return obj_[k]; }
    const Json& at(const std::string& k) const { static const Json n; auto it = obj_.find(k); return it == obj_.end() ? n : it->second; }
    bool has(const std::string& k) const { return obj_.count(k) != 0; }
    const std::map<std::string, Json>& items() const { return obj_; }
    // array
    void push(Json v) { if (type_ != Type::Array) *this = array(); arr_.push_back(std::move(v)); }
    size_t size() const { return type_ == Type::Array ? arr_.size() : obj_.size(); }
    const Json& operator[](size_t i) const { return arr_[i]; }
    const std::vector<Json>& elements() const { return arr_; }

    std::string dump(int indent = 2) const;
    static bool parse(const std::string& text, Json& out, std::string* error = nullptr);
    static bool load(const std::string& path, Json& out, std::string* error = nullptr);
    bool save(const std::string& path) const;

private:
    void dumpTo(std::string& s, int indent, int level) const;
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Json> arr_;
    std::map<std::string, Json> obj_;
};

} // namespace mfx
