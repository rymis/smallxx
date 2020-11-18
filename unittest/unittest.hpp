#pragma once

#include <vector>
#include <map>
#include <string>
#include <utility>
#include <iostream>
#include <memory>
#include <cstring>
#include <chrono>

// Atto Unit Test framework

namespace atto {

    namespace unittest {

		template <typename T>
		struct can_print {
			template <typename A, decltype(std::declval<std::ostream>() << std::declval<A>())>
			static std::true_type check(A& val);

			static std::false_type check(...);

			using type = decltype(check(std::declval<T>()));
		};

		template <typename T>
		inline void print_one(std::ostream& out, T&& x, std::true_type) {
			out << std::forward<T>(x);
		}

		template <typename T>
		inline void print_one(std::ostream& , T&& , std::false_type) {
		}

		inline void println_to(std::ostream& out) {
			out << std::endl;
		}

		template <typename T, typename...F>
		inline void println_to(std::ostream& out, T&& x, F&&...f) {
			print_one(out, std::forward<T>(x), typename can_print<T>::type{});
			println_to(out, std::forward<F>(f)...);
		}

        class unit_test_base {
        public:
            virtual ~unit_test_base() = default;
            virtual void run() = 0;
        };

        enum {
            LARGE_TEST = 1,
            BENCHMARK = 2
        };

        class error : public std::runtime_error {
            int line_;
        public:
            error(int line, const std::string& msg) : runtime_error(msg), line_(line) {
            }

            int line() const {
                return line_;
            }
        };

        class test_storage {
            std::map<std::string, std::pair<unsigned, std::unique_ptr<unit_test_base>>> tests_;
            bool verbose_ = false;

            void usage(std::ostream& out, const char* prog) {
                out << "Usage: " << prog << " [-l] [-a] [test1 [test2...]]" << std::endl;
                out << "   --help, -h         print this message and exit." << std::endl;
                out << "   --list, -l         print list of available tests and exit." << std::endl;
                out << "   --all, -a          run all tests (by default only small)." << std::endl;
                out << "   --benchmarks, -b   run also benchmarks." << std::endl;
                out << "   --verbose, -v      be a little more verbose." << std::endl;
                out << "test1, test2, ... list of tests to run" << std::endl;
            }

            class hwtimer {
                std::chrono::time_point<std::chrono::high_resolution_clock> begin;

            public:
                hwtimer() {
                    begin = std::chrono::high_resolution_clock::now();
                }

                double delta() const {
                    auto end = std::chrono::high_resolution_clock::now();
                    auto delta = end - begin;
                    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
                    auto s = std::chrono::duration_cast<std::chrono::seconds>(delta).count();
                    return s + (ns % 1000000000lu) / 1000000000.0;
                }
            };

            struct Args {
                const char** argv_;
                int argc_;
                int index_;

                Args(int argc, const char* argv[]) : argv_(argv), argc_(argc), index_(1) {
                }

                operator bool () const {
                    return index_ < argc_;
                }

                bool arg(const char* short_name, const char* long_name) {
                    if (index_ < argc_) {
                        if (!std::strcmp(argv_[index_], short_name) || !std::strcmp(argv_[index_], long_name)) {
                            ++index_;
                            return true;
                        }
                    }

                    return false;
                }

                bool arg(const char* short_name, const char* long_name, std::string& value) {
                    if (index_ + 1 < argc_) {
                        if (!std::strcmp(argv_[index_], short_name) || !std::strcmp(argv_[index_], long_name)) {
                            ++index_;
                            value = argv_[index_];
                            ++index_;
                            return true;
                        }
                    }

                    return false;
                }

                bool arg(std::string& value) {
                    if (index_ < argc_) {
                        value = argv_[index_];
                        ++index_;
                        return true;
                    }
                    return false;
                }
            };

        public:
            int run(int argc, const char* argv[]) {
                std::vector<std::string> tests_to_run;
                bool all = false;
                bool benchmarks = false;

                Args args(argc, argv);

                while (args) {
                    if (args.arg("-h", "--help")) {
                        usage(std::cout, argv[0]);
                        return 0;
                    } else if (args.arg("-a", "--all")) {
                        all = true;
                    } else if (args.arg("-b", "--benchmarks")) {
                        benchmarks = true;
                    } else if (args.arg("-l", "--list")) {
                        std::cout << "Available tests {" << std::endl;
                        for (const auto& t : tests_) {
                            std::cout << "    " << t.first << std::endl;
                        }
                        std::cout << "}" << std::endl;
                    } else if (args.arg("-v", "--verbose")) {
                        verbose_ = true;
                    } else {
                        std::string t;
                        args.arg(t);
                        if (!tests_.count(t)) {
                            std::cerr << "Error: unknown test name `" << t << "'" << std::endl;
                            return 1;
                        }
                        tests_to_run.push_back(t);
                    }
                }

                if (tests_to_run.empty()) {
                    for (const auto& t : tests_) {
                        if (t.second.first & LARGE_TEST) {
                            if (all)
                                tests_to_run.push_back(t.first);
                        } else if (t.second.first & BENCHMARK) {
                            if (benchmarks)
                                tests_to_run.push_back(t.first);
                        } else {
                            tests_to_run.push_back(t.first);
                        }
                    }
                }

                for (const auto& name : tests_to_run) {
                    auto& test = tests_[name];

                    if (test.first & LARGE_TEST) {
                        std::cout << "[LARGE TEST ";
                    } else if (test.first & BENCHMARK) {
                        std::cout << "[BENCHMARK ";
                    } else {
                        std::cout << "[SMALL TEST ";
                    }
                    std::cout << name << "]" << std::endl;

                    hwtimer timer;
                    try {
                        test.second->run();
                        std::cout << "[OK " << name << " in " << timer.delta() <<" seconds]" << std::endl;
                    } catch (const error& err) {
                        std::cerr << "[FAILED " << name << ": " << err.what() << " at line " << err.line() << "]" << std::endl;
                    }
                }

                return 0;
            }

            void add(const std::string& name, unsigned flags, std::unique_ptr<unit_test_base> test) {
                auto& t = tests_[name];
                t.first = flags;
                t.second.reset(test.release());
            }
        };

        extern std::unique_ptr<test_storage> global_tests_storage;

        inline test_storage& global_tests() {
            if (!global_tests_storage.get()) {
                global_tests_storage = std::make_unique<test_storage>();
            }
            return *global_tests_storage;
        }

        template <typename Test>
        class test_register_helper {
        public:
            test_register_helper(const char* name, unsigned flags) {
                global_tests().add(name, flags, std::make_unique<Test>());
            }
        };

        template <typename T>
        struct left_value_wrapper {
            const T* value = 0;
            int line = 0;
            const char* message = "internal error";
            bool compared = false;

            left_value_wrapper() = default;
            left_value_wrapper(const left_value_wrapper&) = default;
            left_value_wrapper(left_value_wrapper&&) = default;
            left_value_wrapper& operator = (const left_value_wrapper&) = default;
            left_value_wrapper& operator = (left_value_wrapper&&) = default;

            left_value_wrapper(const T& val) : value(&val) {
            }

            void operator () () {
                if (!compared) {
                    if (!value || !*value) {
                        throw ::atto::unittest::error(line, message);
                    }
                }
            }

            struct check_call {
                void operator () () {
                }
            };

            template <typename R>
            check_call operator == (const R& r) {
                compared = true;

                if (!value) {
                    throw ::atto::unittest::error(line, "internal error");
                }

                if (!(*value == r)) {
                    println_to(std::cerr, "Comparation error: ", *value, " != ", r);
                    throw ::atto::unittest::error(line, message);
                }

                return check_call{};
            }
        };

        struct check_helper {
            int line = 0;
            const char* message = "";

            check_helper(int l, const char* msg) : line(l), message(msg) {
            }

            template <typename T>
            left_value_wrapper<T> operator - (const T& left) {
                left_value_wrapper<T> res(left);
                res.line = line;
                res.message = message;
                return res;
            }
        };

    } // namespace unittest

} // namespace atto

#define ATTO_UNITMAIN() \
    namespace atto { namespace unittest { \
    std::unique_ptr<::atto::unittest::test_storage> global_tests_storage; \
    }} \
    int main(int argc, const char* argv[]) { return ::atto::unittest::global_tests().run(argc, argv); }

#define SMALL_TEST(Name) \
    class Name##__impl : public ::atto::unittest::unit_test_base { \
    public: \
        ~Name##__impl() override = default; \
        void run() override; \
    }; \
    static ::atto::unittest::test_register_helper<Name##__impl> Name##__registrator{#Name, 0}; \
    void Name##__impl::run()

#define TEST(Name) SMALL_TEST(Name)

#define LARGE_TEST(Name) \
    class Name##__impl : public ::atto::unittest::unit_test_base { \
    public: \
        ~Name##__impl() override = default; \
        void run() override; \
    }; \
    static ::atto::unittest::test_register_helper<Name##__impl> Name##__registrator{#Name, ::atto::unittest::LARGE_TEST}; \
    void Name##__impl::run()

#define BENCH(Name) \
    class Name##__impl : public ::atto::unittest::unit_test_base { \
    public: \
        ~Name##__impl() override = default; \
        void run() override; \
    }; \
    static ::atto::unittest::test_register_helper<Name##__impl> Name##__registrator{#Name, ::atto::unittest::BENCHMARK}; \
    void Name##__impl::run()

#define CHECK(pred) \
    do { (::atto::unittest::check_helper(__LINE__, #pred) - pred)(); } while (0)

