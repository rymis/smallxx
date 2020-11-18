#include "sqlitexx.h"
#include "unittest.hpp"
// #include "so_stdoutstream.hpp"
// #include "stream.h"
// #include "reflect.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>

SMALL_TEST(sqlitexx) {
    sqlitexx::DB db{"test_sqlitexx_unittest.db"};
	DEFER(unlink("test_sqlitexx_unittest.db"));

    auto q = db.prepare("CREATE TABLE test (id INTEGER PRIMARY KEY AUTOINCREMENT, text TEXT, x FLOAT, n NUMBER);");
    q.exec();

    for (int i = 0; i < 100; ++i) {
        auto s = db.prepare("INSERT INTO test VALUES(null, ?, ?, ?);", "t_" + std::to_string(i), 1.0 / (i + 1.0), i);
        s.exec();
    }

    {
        auto t = db.transaction();
        auto q2 = db.prepare("SELECT * FROM test;");
        for (auto& row : q2) {
            for (size_t i = 0; i < row.size(); ++i) {
                if (i)
                    std::cout << '\t';
                std::cout << row[i].as_text();
            }
            std::cout << std::endl;
        }
    }
}

#if 0

SMALL_TEST(stdoutstream) {
    opipestream out("cat");
    out << "Hello\n";
    out << "world\n";
}

SMALL_TEST(stream) {
    OutputStream s;
    s << "null output" << std::endl;

    InputStream is;
    char data[7];
    is.read(data, sizeof(data));
    for (size_t i = 0; i < sizeof(data); ++i) {
        CHECK(data[i] == 0);
    }
}

SMALL_TEST(reflect) {
    struct Test {
        int ival = 13;
        float fval = 666.0f;
        std::string sval = "hello";
        bool bval = false;

        REFLECT_STRUCT(
                ival, "int",
                fval, "float",
                sval, "string",
                bval, "bool"
        );
    };

    Test t;
    static_assert(::reflect::detail::check_reflect<Test>::value, "check reflect does not work");
    std::cout << "Print: " << t << std::endl;

    CHECK(reflect::get<0>(t) == 13);
    CHECK(reflect::get<1>(t) == t.fval);
    CHECK(reflect::get<2>(t) == "hello");
    CHECK(reflect::get<3>(t) == false);
    CHECK(reflect::get_name<0>(t) == "int");
    CHECK(reflect::get_name<1>(t) == "float");
    CHECK(reflect::get_name<2>(t) == "string");
    CHECK(reflect::get_name<3>(t) == "bool");
}

#endif

ATTO_UNITMAIN()
