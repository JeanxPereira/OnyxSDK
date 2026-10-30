#include <doctest/doctest.h>

#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/Core/Schema/SchemaJson.h>
#include <Onyx/Core/Schema/SchemaNode.h>
#include <Onyx/Core/Vfs/MemoryFile.h>
#include <Onyx/Core/Vfs/Reader.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace Vfs = Onyx::Vfs;
using namespace Onyx::Schema;

namespace {

Vfs::Reader ReaderOf(std::initializer_list<unsigned> values) {
    std::vector<std::byte> bytes;
    for (unsigned v : values) bytes.push_back(static_cast<std::byte>(v));
    return Vfs::Reader(std::make_shared<Vfs::MemoryFile>(std::move(bytes)));
}

const SchemaNode* Child(const SchemaNode& node, const std::string& name) {
    for (const SchemaNode& c : node.children) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

}

TEST_CASE("Schema.ReadsFixedFieldsInOrder") {
    Schema s("Header");
    s.Field("a", FieldType::U8).Field("b", FieldType::I16).Field("c", FieldType::U32)
        .String("tag", 4).Field("f", FieldType::F32);
    Vfs::Reader r = ReaderOf({0x7F, 0xFE, 0xFF, 0x78, 0x56, 0x34, 0x12, 'A', 'B', 0, 0, 0x00, 0x00, 0x80, 0x3F});
    auto res = ReadSchema(s, r, 0);
    REQUIRE(res.ok());
    const SchemaNode& n = res.value();
    REQUIRE(n.children.size() == 5);
    CHECK(std::get<uint64_t>(Child(n, "a")->value) == 0x7F);
    CHECK(std::get<int64_t>(Child(n, "b")->value) == -2);
    CHECK(std::get<uint64_t>(Child(n, "c")->value) == 0x12345678);
    CHECK(std::get<std::string>(Child(n, "tag")->value) == "AB");
    CHECK(std::get<double>(Child(n, "f")->value) == doctest::Approx(1.0));
    CHECK(Child(n, "c")->offset == 3);
    CHECK(Child(n, "c")->size == 4);
    CHECK(Child(n, "tag")->offset == 7);
}

TEST_CASE("Schema.CountFromEarlierField") {
    auto elem = std::make_shared<Schema>("Elem");
    elem->Field("v", FieldType::U32);
    Schema s("List");
    s.Field("count", FieldType::U16).Array("items", elem, Count::FromField("count"));
    Vfs::Reader r = ReaderOf({3, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0});
    auto res = ReadSchema(s, r, 0);
    REQUIRE(res.ok());
    const SchemaNode* items = Child(res.value(), "items");
    REQUIRE(items);
    REQUIRE(items->children.size() == 3);
    CHECK(std::get<uint64_t>(items->children[2].children[0].value) == 3);

    Schema bad("Bad");
    bad.Array("items", elem, Count::FromField("missing"));
    Vfs::Reader r2 = ReaderOf({0, 0, 0, 0});
    auto res2 = ReadSchema(bad, r2, 0);
    REQUIRE(!res2.ok());
    CHECK(res2.error().message.find("missing") != std::string::npos);
}

TEST_CASE("Schema.OffsetFromField") {
    auto inner = std::make_shared<Schema>("Inner");
    inner->Field("x", FieldType::U8).Field("y", FieldType::U8);
    Schema s("Outer");
    s.Field("ptr", FieldType::U32).Struct("inner", inner, Offset::FromField("ptr"))
        .Field("after", FieldType::U8);
    Vfs::Reader r = ReaderOf({8, 0, 0, 0, 0, 0, 0, 0, 5, 6, 9});
    auto res = ReadSchema(s, r, 0);
    REQUIRE(res.ok());
    const SchemaNode* in = Child(res.value(), "inner");
    REQUIRE(in);
    CHECK(in->offset == 8);
    CHECK(std::get<uint64_t>(Child(*in, "y")->value) == 6);
    CHECK(std::get<uint64_t>(Child(res.value(), "after")->value) == 9);
    CHECK(Child(res.value(), "after")->offset == 10);

    Schema rel("Rel");
    rel.Field("z", FieldType::U8, Offset::FromStructStart(2));
    Vfs::Reader r2 = ReaderOf({0, 0, 0, 0, 0, 0, 0, 0, 7});
    auto res2 = ReadSchema(rel, r2, 6);
    REQUIRE(res2.ok());
    CHECK(std::get<uint64_t>(res2.value().children[0].value) == 7);
}

TEST_CASE("Schema.BigEndianPerSchema") {
    Schema s("Be");
    s.BigEndian(true).Field("v", FieldType::U32);
    Vfs::Reader r = ReaderOf({0x12, 0x34, 0x56, 0x78});
    auto res = ReadSchema(s, r, 0);
    REQUIRE(res.ok());
    CHECK(std::get<uint64_t>(res.value().children[0].value) == 0x12345678);
    CHECK(!r.BigEndian());
}

TEST_CASE("Schema.U64KeepsFullPrecision") {
    Schema s("Big");
    s.Field("v", FieldType::U64).Field("n", FieldType::I64);
    Vfs::Reader r = ReaderOf({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80});
    auto res = ReadSchema(s, r, 0);
    REQUIRE(res.ok());
    CHECK(std::get<uint64_t>(res.value().children[0].value) == 0xFFFFFFFFFFFFFFFFull);
    const std::string json = ToJson(res.value());
    CHECK(json.find("18446744073709551615") != std::string::npos);
    CHECK(json.find("-9223372036854775808") != std::string::npos);
    CHECK(json.find('e') == std::string::npos);
}

TEST_CASE("Schema.ShortFileReturnsErrorWithPartialTree") {
    Schema s("Short");
    s.Field("first", FieldType::U32).Field("second", FieldType::U32);
    Vfs::Reader r = ReaderOf({1, 0, 0, 0, 2, 0});
    auto res = ReadSchema(s, r, 0);
    REQUIRE(!res.ok());
    CHECK(res.error().message.find("second") != std::string::npos);
}

TEST_CASE("Schema.NestedStructRangeCoversChildren") {
    auto inner = std::make_shared<Schema>("Inner");
    inner->Field("a", FieldType::U16).Pad(2).Field("b", FieldType::U32).Bytes("raw", 2);
    Schema s("Outer");
    s.Field("head", FieldType::U8).Struct("inner", inner);
    Vfs::Reader r = ReaderOf({1, 1, 0, 0, 0, 2, 0, 0, 0, 0xAB, 0xCD});
    auto res = ReadSchema(s, r, 0);
    REQUIRE(res.ok());
    const SchemaNode* in = Child(res.value(), "inner");
    REQUIRE(in);
    CHECK(in->offset == 1);
    CHECK(in->size == 10);
    CHECK(in->children.size() == 3);
    for (const SchemaNode& c : in->children) {
        CHECK(c.offset >= in->offset);
        CHECK(c.offset + c.size <= in->offset + in->size);
    }
    const std::string json = ToJson(res.value());
    CHECK(json.find("\"raw\":\"abcd\"") != std::string::npos);
}
