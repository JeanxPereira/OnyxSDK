#include <Onyx/Core/Schema/SchemaNode.h>

#include <algorithm>
#include <format>

namespace Onyx::Schema {

namespace {

constexpr int kMaxDepth = 64;

Error MakeError(std::string message) { return Error{"schema", std::move(message), 0}; }

bool IsInteger(FieldType t) {
    switch (t) {
    case FieldType::U8: case FieldType::I8: case FieldType::U16: case FieldType::I16:
    case FieldType::U32: case FieldType::I32: case FieldType::U64: case FieldType::I64:
        return true;
    default:
        return false;
    }
}

template <class T>
bool ReadInt(Vfs::Reader& r, SchemaNode& node) {
    auto v = r.Read<T>();
    node.size = sizeof(T);
    if (!v) return false;
    if constexpr (std::is_signed_v<T>) node.value = static_cast<int64_t>(*v);
    else node.value = static_cast<uint64_t>(*v);
    return true;
}

const SchemaNode* FindField(const std::vector<SchemaNode>& siblings, const std::string& name) {
    for (auto it = siblings.rbegin(); it != siblings.rend(); ++it) {
        if (it->name == name) return &*it;
    }
    return nullptr;
}

Result<uint64_t> IntegerOf(const std::vector<SchemaNode>& siblings, const std::string& name,
                           const std::string& user) {
    const SchemaNode* ref = FindField(siblings, name);
    if (!ref) {
        return Result<uint64_t>::Fail(
            MakeError(std::format("field '{}' references unknown field '{}'", user, name)));
    }
    if (!IsInteger(ref->type)) {
        return Result<uint64_t>::Fail(
            MakeError(std::format("field '{}' references non-integer field '{}'", user, name)));
    }
    if (const auto* u = std::get_if<uint64_t>(&ref->value)) return Result<uint64_t>::Ok(*u);
    const int64_t s = std::get<int64_t>(ref->value);
    if (s < 0) {
        return Result<uint64_t>::Fail(
            MakeError(std::format("field '{}' references negative field '{}'", user, name)));
    }
    return Result<uint64_t>::Ok(static_cast<uint64_t>(s));
}

Result<SchemaNode> ReadStruct(const Schema& schema, Vfs::Reader& r, uint64_t structStart,
                              const std::string& nodeName, int depth);

Result<SchemaNode> ReadField(const FieldDef& def, Vfs::Reader& r, const std::vector<SchemaNode>& siblings,
                             int depth) {
    using R = Result<SchemaNode>;
    SchemaNode node;
    bool ok = true;
    switch (def.type) {
    case FieldType::U8: ok = ReadInt<uint8_t>(r, node); break;
    case FieldType::I8: ok = ReadInt<int8_t>(r, node); break;
    case FieldType::U16: ok = ReadInt<uint16_t>(r, node); break;
    case FieldType::I16: ok = ReadInt<int16_t>(r, node); break;
    case FieldType::U32: ok = ReadInt<uint32_t>(r, node); break;
    case FieldType::I32: ok = ReadInt<int32_t>(r, node); break;
    case FieldType::U64: ok = ReadInt<uint64_t>(r, node); break;
    case FieldType::I64: ok = ReadInt<int64_t>(r, node); break;
    case FieldType::F32: {
        auto v = r.Read<float>();
        ok = v.has_value();
        if (ok) node.value = static_cast<double>(*v);
        node.size = 4;
        break;
    }
    case FieldType::F64: {
        auto v = r.Read<double>();
        ok = v.has_value();
        if (ok) node.value = *v;
        node.size = 8;
        break;
    }
    case FieldType::String: {
        node.size = def.length;
        ok = def.length <= r.Remaining();
        if (ok) {
            auto s = r.ReadString(static_cast<size_t>(def.length));
            ok = s.has_value();
            if (ok) {
                const size_t nul = s->find('\0');
                if (nul != std::string::npos) s->resize(nul);
                node.value = std::move(*s);
            }
        }
        break;
    }
    case FieldType::Bytes: {
        node.size = def.length;
        ok = def.length <= r.Remaining();
        if (ok) {
            std::vector<std::byte> buf(static_cast<size_t>(def.length));
            ok = r.Read(std::span<std::byte>(buf)) == buf.size();
            if (ok) node.value = std::move(buf);
        }
        break;
    }
    case FieldType::Struct: {
        if (!def.element) return R::Fail(MakeError(std::format("struct field '{}' has no schema", def.name)));
        return ReadStruct(*def.element, r, r.Tell(), def.name, depth + 1);
    }
    case FieldType::Array: {
        if (!def.element) {
            return R::Fail(MakeError(std::format("array field '{}' has no element schema", def.name)));
        }
        uint64_t count = def.count.value;
        if (def.count.kind == Count::Kind::FromField) {
            auto c = IntegerOf(siblings, def.count.field, def.name);
            if (!c.ok()) return R::Fail(c.error());
            count = c.value();
        }
        if (count > r.Size()) {
            return R::Fail(MakeError(std::format("array field '{}' count {} exceeds the data", def.name, count)));
        }
        node.offset = r.Tell();
        for (uint64_t i = 0; i < count; ++i) {
            auto e = ReadStruct(*def.element, r, r.Tell(), std::format("[{}]", i), depth + 1);
            if (!e.ok()) return R::Fail(e.error());
            r.Seek(e.value().offset + e.value().size);
            node.children.push_back(std::move(e.value()));
        }
        node.size = r.Tell() - node.offset;
        return R::Ok(std::move(node));
    }
    case FieldType::Pad:
        break;
    }
    if (!ok) return R::Fail(MakeError(std::format("short read at field '{}'", def.name)));
    return R::Ok(std::move(node));
}

Result<SchemaNode> ReadStruct(const Schema& schema, Vfs::Reader& r, uint64_t structStart,
                              const std::string& nodeName, int depth) {
    using R = Result<SchemaNode>;
    if (depth > kMaxDepth) return R::Fail(MakeError(std::format("schema '{}' nests too deeply", schema.Name())));

    const bool savedEndian = r.BigEndian();
    r.SetBigEndian(schema.IsBigEndian());
    struct Restore {
        Vfs::Reader& reader;
        bool value;
        ~Restore() { reader.SetBigEndian(value); }
    } restore{r, savedEndian};

    SchemaNode out;
    out.name = nodeName;
    out.type = FieldType::Struct;
    uint64_t lo = r.Tell();
    uint64_t hi = lo;
    uint64_t cursor = lo;

    for (const FieldDef& def : schema.Fields()) {
        uint64_t pos = cursor;
        switch (def.offset.kind) {
        case Offset::Kind::Next: break;
        case Offset::Kind::Absolute: pos = def.offset.value; break;
        case Offset::Kind::FromStructStart: pos = structStart + def.offset.value; break;
        case Offset::Kind::FromField: {
            auto v = IntegerOf(out.children, def.offset.field, def.name);
            if (!v.ok()) return R::Fail(v.error());
            pos = v.value();
            break;
        }
        }
        if (pos > r.Size()) return R::Fail(MakeError(std::format("short read at field '{}'", def.name)));
        r.Seek(pos);

        if (def.type == FieldType::Pad) {
            if (def.length > r.Remaining()) return R::Fail(MakeError("short read at padding"));
            cursor = pos + def.length;
            hi = std::max(hi, cursor);
            continue;
        }

        auto field = ReadField(def, r, out.children, depth);
        if (!field.ok()) return R::Fail(field.error());
        SchemaNode node = std::move(field.value());
        node.name = def.name;
        node.type = def.type;
        if (def.type != FieldType::Struct && def.type != FieldType::Array) node.offset = pos;
        cursor = node.offset + node.size;
        lo = std::min(lo, node.offset);
        hi = std::max(hi, cursor);
        r.Seek(cursor);
        out.children.push_back(std::move(node));
    }

    out.offset = lo;
    out.size = hi - lo;
    r.Seek(hi);
    return R::Ok(std::move(out));
}

}

Result<SchemaNode> ReadSchema(const Schema& schema, Vfs::Reader& reader, uint64_t structStart) {
    if (structStart > reader.Size()) {
        return Result<SchemaNode>::Fail(MakeError(std::format("struct start of '{}' is past the data", schema.Name())));
    }
    reader.Seek(structStart);
    return ReadStruct(schema, reader, structStart, schema.Name(), 0);
}

}
