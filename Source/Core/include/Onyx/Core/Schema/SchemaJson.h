#pragma once

#include <Onyx/Core/Schema/SchemaNode.h>

#include <string>

namespace Onyx::Schema {

std::string ToJson(const SchemaNode& node);

}
