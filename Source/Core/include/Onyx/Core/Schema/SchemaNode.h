#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/Core/Vfs/Reader.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Onyx::Schema {

struct SchemaNode {
    std::string name;
    FieldType type = FieldType::U8;
    std::variant<std::monostate, int64_t, uint64_t, double, std::string, std::vector<std::byte>> value;
    uint64_t offset = 0;
    uint64_t size = 0;
    std::vector<SchemaNode> children;
};

Result<SchemaNode> ReadSchema(const Schema& schema, Vfs::Reader& reader, uint64_t structStart);

}
