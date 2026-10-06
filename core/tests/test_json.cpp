#include "doctest.h"
#include "pron/json_writer.h"

#include <cmath>
#include <limits>

using namespace pron;

TEST_SUITE("json") {
    TEST_CASE("escaping") {
        CHECK(JsonWriter::escape("plain") == "\"plain\"");
        CHECK(JsonWriter::escape("a\"b\\c") == "\"a\\\"b\\\\c\"");
        CHECK(JsonWriter::escape("line\nnext\ttab\r") == "\"line\\nnext\\ttab\\r\"");
        CHECK(JsonWriter::escape(std::string("\x01", 1)) == "\"\\u0001\"");
        CHECK(JsonWriter::escape("звук θ") == "\"звук θ\"");  // UTF-8 passes through
    }

    TEST_CASE("number formatting is locale independent and trimmed") {
        CHECK(JsonWriter::format_number(0.0, 3) == "0");
        CHECK(JsonWriter::format_number(1.5, 3) == "1.5");
        CHECK(JsonWriter::format_number(-2.25, 1) == "-2.3");  // llround: half away from zero
        CHECK(JsonWriter::format_number(87.5, 1) == "87.5");
        CHECK(JsonWriter::format_number(100.0, 1) == "100");
        CHECK(JsonWriter::format_number(0.0004, 3) == "0");
        CHECK(JsonWriter::format_number(-0.0004, 3) == "0");
        CHECK(JsonWriter::format_number(0.05, 3) == "0.05");
        CHECK(JsonWriter::format_number(12.3456, 2) == "12.35");
        CHECK(JsonWriter::format_number(std::nan(""), 2) == "null");
        CHECK(JsonWriter::format_number(std::numeric_limits<double>::infinity(), 2) == "null");
    }

    TEST_CASE("compact document") {
        JsonWriter j;
        j.begin_object();
        j.kv("name", "θ");
        j.kv("n", 3);
        j.kv("x", 0.25);
        j.kv("ok", true);
        j.key("none").null();
        j.key("arr").begin_array();
        j.value(1).value("two");
        j.begin_object().end_object();
        j.begin_array().end_array();
        j.end_array();
        j.end_object();
        CHECK(j.str() == "{\"name\":\"θ\",\"n\":3,\"x\":0.25,\"ok\":true,\"none\":null,\"arr\":[1,\"two\",{},[]]}");
    }

    TEST_CASE("pretty document") {
        JsonWriter j(true);
        j.begin_object();
        j.kv("a", 1);
        j.key("b").begin_array().value(2).end_array();
        j.end_object();
        CHECK(j.str() == "{\n  \"a\": 1,\n  \"b\": [\n    2\n  ]\n}");
    }
}
