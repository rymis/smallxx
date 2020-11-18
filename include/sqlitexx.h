#pragma once

#include <string>
#include <memory>
#include <sstream>
#include <sqlite3/sqlite3.h>

/**
 * Simple header-only sqlite3 wrapper for C++
 */

namespace sqlitexx {

    class Error: public std::runtime_error {
        template <typename A>
        static bool write_one(std::ostream& s, A&& x) {
            s << std::forward<A>(x);
            return true;
        }

        template <typename...A>
        static std::string write_all(A&&...args) {
            std::ostringstream ss;
            bool dummy[sizeof...(A)] = { write_one(ss, std::forward<A>(args))... };
            (void)dummy;
            return ss.str();
        }

        int code_ = 0;
    public:
        Error(int res, const std::string& s) : runtime_error(s), code_(res) {
        }

        template <typename...A>
        Error(int res, A&&...args) : runtime_error(write_all(std::forward<A>(args)...)), code_(res) {
        }

        int code() const {
            return code_;
        }
    };

    class Statement {
        struct STMTFinalizer {
            void operator () (sqlite3_stmt* stmt) {
                sqlite3_finalize(stmt);
            }
        };

        std::unique_ptr<sqlite3_stmt, STMTFinalizer> stmt_;
    public:
        Statement(sqlite3_stmt* stmt) : stmt_(stmt) {
        }

        Statement(const Statement& st) = delete;
        Statement(Statement&& st) = default;
        Statement& operator = (const Statement& st) = delete;
        Statement& operator = (Statement&& st) = default;

        void bind(unsigned pos, const std::string& value) {
            int res;
            if ((res = sqlite3_bind_text(stmt_.get(), pos, value.c_str(), value.size(), SQLITE_TRANSIENT)) != SQLITE_OK) {
                throw Error(res, "bind failed");
            }
        }

        void bind(unsigned pos, bool x) {
            int res;
            if ((res = sqlite3_bind_int(stmt_.get(), pos, x)) != SQLITE_OK) {
                throw Error(res, "bind failed");
            }
        }

        void bind(unsigned pos, int32_t x) {
            int res;
            if ((res = sqlite3_bind_int(stmt_.get(), pos, x)) != SQLITE_OK) {
                throw Error(res, "bind failed");
            }
        }

        void bind(unsigned pos, int64_t x) {
            int res;
            if ((res = sqlite3_bind_int64(stmt_.get(), pos, x)) != SQLITE_OK) {
                throw Error(res, "bind failed");
            }
        }

        void bind(unsigned pos, std::nullptr_t) {
            int res;
            if ((res = sqlite3_bind_null(stmt_.get(), pos)) != SQLITE_OK) {
                throw Error(res, "bind failed: ");
            }
        }

        void bind(unsigned pos, double x) {
            int res;
            if ((res = sqlite3_bind_double(stmt_.get(), pos, x)) != SQLITE_OK) {
                throw Error(res, "bind failed");
            }
        }

        template <typename T>
        void bind(const std::string& argname, T&& x) {
            int n = sqlite3_bind_parameter_index(stmt_.get(), argname.c_str());
            if (n == 0) {
                throw Error(SQLITE_ERROR, "unknown parameter name '", argname, "'");
            }

            bind(static_cast<unsigned>(n), std::forward<T>(x));
        }

        //! execute query and return single result (for select) or empty string (for other queries)
        std::string exec() {
            int res = sqlite3_step(stmt_.get());

            if (res == SQLITE_DONE) {
                return "";
            } else if (res == SQLITE_ROW) {
                // Here we should take only one value from row:
                if (sqlite3_column_count(stmt_.get())) {
                    const char* data = (const char*)sqlite3_column_text(stmt_.get(), 0);
                    if (!data) {
                        return "";
                    }

                    return std::string{data};
                }

                return "";
            } else {
                throw Error(res, "execution failed");
            }
        }

        class Value {
            sqlite3_stmt* stmt_ = nullptr;
            unsigned index_ = 0;
        public:
            Value() = default;
            Value(const Value&) = default;
            Value(Value&&) = default;
            Value& operator = (const Value&) = default;
            Value& operator = (Value&&) = default;

            Value(sqlite3_stmt* stmt, unsigned index): stmt_(stmt), index_(index) {
            }

            sqlite3_stmt* get() {
                return stmt_;
            }

            int type() {
                return sqlite3_column_type(stmt_, index_);
            }

            bool is_int() {
                return type() == SQLITE_INTEGER;
            }

            int64_t as_int() {
                return sqlite3_column_int64(stmt_, index_);
            }

            bool is_double() {
                return type() == SQLITE_FLOAT;
            }

            double as_double() {
                return sqlite3_column_double(stmt_, index_);
            }

            bool is_text() {
                return type() == SQLITE_TEXT;
            }

            std::string as_text() {
                const char* data = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index_));
                if (!data)
                    return "";

                if (is_text() || is_blob()) {
                    size_t len = sqlite3_column_bytes(stmt_, index_);
                    return std::string{data, len};
                } else {
                    return std::string{data};
                }
            }

            bool is_blob() {
                return type() == SQLITE_BLOB;
            }

            std::string as_blob() {
                const char* data = reinterpret_cast<const char*>(sqlite3_column_blob(stmt_, index_));
                if (!data)
                    return "";

                size_t len = sqlite3_column_bytes(stmt_, index_);

                return std::string{data, len};
            }

            operator double () {
                return as_double();
            }

            operator std::string () {
                return as_text();
            }

            operator int () {
                return as_int();
            }

            operator long() {
                return as_int();
            }
        };

        Value operator [] (unsigned idx) {
            if (!stmt_ || (int)idx >= sqlite3_column_count(stmt_.get())) {
                throw Error(SQLITE_ERROR, "column index ", idx, " is out of range");
            }

            return Value(stmt_.get(), idx);
        }

        size_t size() {
            return sqlite3_column_count(stmt_.get());
        }

        bool step() {
            int res = sqlite3_step(stmt_.get());
            if (res == SQLITE_ROW) {
                return true;
            }

            if (res == SQLITE_DONE)
                return false;

            throw Error(res, "execution failed");
        }

        struct iterator {
            Statement& stmt;
            bool the_end = false;

            iterator(Statement& st, bool end) : stmt(st), the_end(end) {
            }

            Statement& operator * () {
                return stmt;
            }

            Statement* operator -> () {
                return &stmt;
            }

            iterator& operator ++ () {
                if (!stmt.step())
                    the_end = true;
                return *this;
            }

            bool operator == (const iterator& it) const {
                return (it.the_end && the_end);
            }

            bool operator != (const iterator& it) const {
                return !operator==(it);
            }
        };

        iterator begin() {
            return iterator{*this, false};
        }

        iterator end() {
            return iterator{*this, true};
        }

    };

    //! Transaction which commits automatically
    class Transaction {
        sqlite3* db_ = nullptr;
        bool done_ = false;

        int exec(const char* sql) {
            sqlite3_stmt* stmt = nullptr;
            const char* end = nullptr;
            int res = sqlite3_prepare_v2(db_, sql, -1, &stmt, &end);
            if (res != SQLITE_OK) {
                return res;
            }
            res = sqlite3_step(stmt);
            sqlite3_finalize(stmt);

            if (res != SQLITE_DONE) {
                return res;
            }

            return SQLITE_OK;
        }

        // TODO: use SAVEPOINTS so nested transactions would work
        bool try_commit(bool exc) {
            for (;;) {
                int res = exec("COMMIT;");
                if (res == SQLITE_OK) {
                    done_ = true;
                    return true;
                }

                if (res != SQLITE_BUSY) {
                    if (exc) {
                        throw Error(res, "commit failed");
                    } else {
                        return false;
                    }
                }
            }
        }

    public:
        Transaction(const Transaction& t) = delete;
        Transaction& operator = (const Transaction& t) = delete;

        Transaction(Transaction&& t) {
            if (db_ && !done_) {
                commit();
            }

            db_ = t.db_;
            done_ = t.done_;

            t.db_ = nullptr;
        }

        Transaction& operator = (Transaction&& t) {
            if (db_ && !done_) {
                commit();
            }

            db_ = t.db_;
            done_ = t.done_;

            t.db_ = nullptr;

            return *this;
        }

        Transaction(sqlite3* db) : db_(db) {
            int res;
            if ((res = exec("BEGIN TRANSACTION;")) != SQLITE_OK)
                throw Error(res, "can't begin transaction");
            done_ = false;
        }

        ~Transaction() {
            try_commit(false);
        }

        void commit() {
            try_commit(true);
        }

        void rollback() {
            int res;
            if ((res = exec("ROLLBACK;")) != SQLITE_OK) {
                throw Error(res, "rollback failed");
            }
            done_ = true;
        }
    };

    class DB {
        struct DBCloser {
            void operator () (sqlite3* db) const {
                sqlite3_close(db);
            }
        };

        std::unique_ptr<sqlite3, DBCloser> db_;

        void bind_all(Statement&, unsigned) const {
        }

        template <typename T, typename...A>
        void bind_all(Statement& stmt, unsigned idx, T&& x, A&&...args) const {
            stmt.bind(idx, std::forward<T>(x));
            bind_all(stmt, idx + 1, std::forward<A>(args)...);
        }

    public:
        //! open memory database.
        DB() {
            open(":memory:");
        }

        //! open database from file.
        DB(const std::string& name) {
            open(name);
        }

        //! create wrapper for opened database.
        DB(sqlite3* db) : db_(db) {
        }

        // explicitly delete default copy operations (it is not nescessery because they were deleted in unique_ptr)
        DB(const DB&) = delete;
        DB(DB&&) = delete;
        DB& operator = (const DB&) = delete;
        DB& operator = (DB&&) = delete;

        //! open database:
        void open(const std::string& name) {
            sqlite3* db = nullptr;
            int res;
            if ((res = sqlite3_open(name.c_str(), &db)) != SQLITE_OK) {
                throw Error(res, "sqlite3 error");
            }
            db_.reset(db);
        }

        sqlite3* get() {
            return db_.get();
        }

        Statement prepare(const std::string& stmt) {
            sqlite3_stmt* st = nullptr;
            const char* end = nullptr;
            const char* start = stmt.c_str();

            int res;
            if ((res = sqlite3_prepare_v2(db_.get(), start, stmt.size(), &st, &end)) != SQLITE_OK) {
                throw Error(res, "prepare failed");
            }

            return Statement(st);
        }

        template <typename...A>
        Statement prepare(const std::string& stmt, A&&...args) {
            Statement st = prepare(stmt);

            bind_all(st, 1, std::forward<A>(args)...);

            return st;
        }

        Transaction transaction() {
            return Transaction(db_.get());
        }
    };

} // namespace sqlitexx
