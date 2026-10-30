#include <Onyx/Core/Schema/Schema.h>

namespace Onyx::Schema {

Schema::Schema(std::string name) : m_name(std::move(name)) {}

Schema& Schema::Field(std::string name, FieldType type, Offset offset) {
    FieldDef def;
    def.name = std::move(name);
    def.type = type;
    def.offset = std::move(offset);
    m_fields.push_back(std::move(def));
    return *this;
}

Schema& Schema::String(std::string name, size_t length, Offset offset) {
    Field(std::move(name), FieldType::String, std::move(offset));
    m_fields.back().length = length;
    return *this;
}

Schema& Schema::Bytes(std::string name, size_t length, Offset offset) {
    Field(std::move(name), FieldType::Bytes, std::move(offset));
    m_fields.back().length = length;
    return *this;
}

Schema& Schema::Pad(size_t length) {
    Field({}, FieldType::Pad);
    m_fields.back().length = length;
    return *this;
}

Schema& Schema::Struct(std::string name, std::shared_ptr<const Schema> schema, Offset offset) {
    Field(std::move(name), FieldType::Struct, std::move(offset));
    m_fields.back().element = std::move(schema);
    return *this;
}

Schema& Schema::Array(std::string name, std::shared_ptr<const Schema> element, Count count, Offset offset) {
    Field(std::move(name), FieldType::Array, std::move(offset));
    m_fields.back().element = std::move(element);
    m_fields.back().count = std::move(count);
    return *this;
}

Schema& Schema::BigEndian(bool bigEndian) {
    m_bigEndian = bigEndian;
    return *this;
}

}
