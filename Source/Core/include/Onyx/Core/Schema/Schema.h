#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Onyx::Schema {

enum class FieldType { U8, I8, U16, I16, U32, I32, U64, I64, F32, F64, String, Bytes, Struct, Array, Pad };

struct Offset {
    enum class Kind { Next, Absolute, FromStructStart, FromField };

    Kind kind = Kind::Next;
    uint64_t value = 0;
    std::string field;

    static Offset Next() { return {}; }
    static Offset Absolute(uint64_t position) { return {Kind::Absolute, position, {}}; }
    static Offset FromStructStart(uint64_t delta) { return {Kind::FromStructStart, delta, {}}; }
    static Offset FromField(std::string name) { return {Kind::FromField, 0, std::move(name)}; }
};

struct Count {
    enum class Kind { Constant, FromField };

    Kind kind = Kind::Constant;
    uint64_t value = 0;
    std::string field;

    static Count Of(uint64_t n) { return {Kind::Constant, n, {}}; }
    static Count FromField(std::string name) { return {Kind::FromField, 0, std::move(name)}; }
};

class Schema;

struct FieldDef {
    std::string name;
    FieldType type = FieldType::U8;
    Offset offset;
    uint64_t length = 0;
    std::shared_ptr<const Schema> element;
    Count count;
};

class Schema {
public:
    explicit Schema(std::string name);

    Schema& Field(std::string name, FieldType type, Offset offset = Offset::Next());
    Schema& String(std::string name, size_t length, Offset offset = Offset::Next());
    Schema& Bytes(std::string name, size_t length, Offset offset = Offset::Next());
    Schema& Pad(size_t length);
    Schema& Struct(std::string name, std::shared_ptr<const Schema> schema, Offset offset = Offset::Next());
    Schema& Array(std::string name, std::shared_ptr<const Schema> element, Count count,
                  Offset offset = Offset::Next());
    Schema& BigEndian(bool bigEndian);

    const std::string& Name() const { return m_name; }
    bool IsBigEndian() const { return m_bigEndian; }
    const std::vector<FieldDef>& Fields() const { return m_fields; }

private:
    std::string m_name;
    bool m_bigEndian = false;
    std::vector<FieldDef> m_fields;
};

}
