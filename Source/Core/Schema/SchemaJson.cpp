#include <Onyx/Core/Schema/SchemaJson.h>

#include <charconv>
#include <cmath>

namespace Onyx::Schema {

namespace {

void AppendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                static constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0xF];
            } else {
                out += ch;
            }
        }
    }
    out += '"';
}

void AppendDouble(std::string& out, double d) {
    if (!std::isfinite(d)) {
        out += "null";
        return;
    }
    char buf[40];
    auto res = std::to_chars(buf, buf + sizeof(buf), d);
    out.append(buf, res.ptr);
}

void Write(std::string& out, const SchemaNode& node) {
    if (node.type == FieldType::Struct) {
        out += '{';
        for (size_t i = 0; i < node.children.size(); ++i) {
            if (i) out += ',';
            AppendEscaped(out, node.children[i].name);
            out += ':';
            Write(out, node.children[i]);
        }
        out += '}';
        return;
    }
    if (node.type == FieldType::Array) {
        out += '[';
        for (size_t i = 0; i < node.children.size(); ++i) {
            if (i) out += ',';
            Write(out, node.children[i]);
        }
        out += ']';
        return;
    }
    if (const auto* i = std::get_if<int64_t>(&node.value)) {
        out += std::to_string(*i);
    } else if (const auto* u = std::get_if<uint64_t>(&node.value)) {
        out += std::to_string(*u);
    } else if (const auto* d = std::get_if<double>(&node.value)) {
        AppendDouble(out, *d);
    } else if (const auto* s = std::get_if<std::string>(&node.value)) {
        AppendEscaped(out, *s);
    } else if (const auto* b = std::get_if<std::vector<std::byte>>(&node.value)) {
        static constexpr char hex[] = "0123456789abcdef";
        out += '"';
        for (const std::byte v : *b) {
            const auto n = static_cast<unsigned>(v);
            out += hex[n >> 4];
            out += hex[n & 0xF];
        }
        out += '"';
    } else {
        out += "null";
    }
}

}

std::string ToJson(const SchemaNode& node) {
    std::string out;
    Write(out, node);
    return out;
}

}
