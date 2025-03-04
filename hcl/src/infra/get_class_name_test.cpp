#include "get_class_name.hpp"
#include <memory>
#ifdef COMPILE_TIME_GET_CLASS_NAME_SUPPORTED
// compile-time verification of getClassName
// the code IS NOT executed at runtime
// only compile-time checks are performed

[[maybe_unused]] static void testFn()
{
    static_assert(CLASS_NAME.empty(), "getClassName failed for function scope");
}

class TestClass
{
public:
    TestClass() { static_assert(CLASS_NAME == "TestClass", "getClassName failed for class scope"); }
    TestClass(int, std::shared_ptr<int>)
    {
        static_assert(CLASS_NAME == "TestClass", "getClassName failed for class scope");
    }
    virtual ~TestClass() { static_assert(CLASS_NAME == "TestClass", "getClassName failed for class scope"); }
    void f(int, std::shared_ptr<int>)
    {
        static_assert(CLASS_NAME == "TestClass", "getClassName failed for class scope");
        [[maybe_unused]] auto lambda = []() {
            static_assert(CLASS_NAME == "TestClass", "getClassName failed for lambda scope");
        };
        fTemplated<int>(0, nullptr);
    }

    virtual const int f2(int, std::shared_ptr<int>)
    {
        static_assert(CLASS_NAME == "TestClass", "getClassName failed for class scope");
        [[maybe_unused]] auto lambda = []() {
            static_assert(CLASS_NAME == "TestClass", "getClassName failed for lambda scope");
        };
        return 0;
    }

    static const int f3(int, std::shared_ptr<int>)
    {
        static_assert(CLASS_NAME == "TestClass", "getClassName failed for class scope");
        [[maybe_unused]] auto lambda = []() {
            static_assert(CLASS_NAME == "TestClass", "getClassName failed for lambda scope");
        };
        return 0;
    }

    template<class T>
    void fTemplated(int, std::shared_ptr<T>)
    {
        static_assert(CLASS_NAME == "TestClass", "getClassName failed for class scope");
    }
};

namespace ns
{

[[maybe_unused]] static void testFn()
{
    static_assert(CLASS_NAME == "ns", "getClassName failed for function scope");
}
class TestClass
{
public:
    class NestedClass
    {
        NestedClass()
        {
            static_assert(CLASS_NAME == "ns::TestClass::NestedClass", "getClassName failed for nested class scope");
        }
        NestedClass(int, std::shared_ptr<int>, int)
        {
            static_assert(CLASS_NAME == "ns::TestClass::NestedClass", "getClassName failed for nested class scope");
        }
        ~NestedClass()
        {
            static_assert(CLASS_NAME == "ns::TestClass::NestedClass", "getClassName failed for nested class scope");
        }
    };
    TestClass() { static_assert(CLASS_NAME == "ns::TestClass", "getClassName failed for class scope"); }
    TestClass(int, std::shared_ptr<int>, int)
    {
        static_assert(CLASS_NAME == "ns::TestClass", "getClassName failed for class scope");
    }
    ~TestClass() { static_assert(CLASS_NAME == "ns::TestClass", "getClassName failed for class scope"); }
    void f(int, std::shared_ptr<int>)
    {
        static_assert(CLASS_NAME == "ns::TestClass", "getClassName failed for class scope");
        [[maybe_unused]] auto lambda = []() {
            static_assert(CLASS_NAME == "ns::TestClass", "getClassName failed for lambda scope");
        };
        fTemplated<int>(0, nullptr);
    }
    template<class T>
    void fTemplated(int, std::shared_ptr<T>)
    {
        static_assert(CLASS_NAME == "ns::TestClass", "getClassName failed for class scope");
    }
};

template<class T>
class TestClassTemplated
{
public:
    TestClassTemplated()
    {
        static_assert(CLASS_NAME == "ns::TestClassTemplated", "getClassName failed for class scope");
    }
    TestClassTemplated(int, std::shared_ptr<int>)
    {
        static_assert(CLASS_NAME == "ns::TestClassTemplated", "getClassName failed for class scope");
    }
    ~TestClassTemplated()
    {
        static_assert(CLASS_NAME == "ns::TestClassTemplated", "getClassName failed for class scope");
    }
    void f(int, std::shared_ptr<int>)
    {
        static_assert(CLASS_NAME == "ns::TestClassTemplated", "getClassName failed for class scope");
        [[maybe_unused]] auto lambda = []() {
            static_assert(CLASS_NAME == "ns::TestClassTemplated", "getClassName failed for lambda scope");
        };
        fTemplated<int>(0, nullptr);
    }
    template<class T2>
    void fTemplated(int, std::shared_ptr<T2>)
    {
        static_assert(CLASS_NAME == "ns::TestClassTemplated", "getClassName failed for class scope");
    }
};
namespace ns2
{
class TestClass
{
public:
    class NestedClass
    {
        NestedClass()
        {
            static_assert(CLASS_NAME == "ns::ns2::TestClass::NestedClass",
                          "getClassName failed for nested class scope");
        }
        NestedClass(int, std::shared_ptr<int>, int)
        {
            static_assert(CLASS_NAME == "ns::ns2::TestClass::NestedClass",
                          "getClassName failed for nested class scope");
        }
        ~NestedClass()
        {
            static_assert(CLASS_NAME == "ns::ns2::TestClass::NestedClass",
                          "getClassName failed for nested class scope");
        }
    };
    TestClass() { static_assert(CLASS_NAME == "ns::ns2::TestClass", "getClassName failed for class scope"); }
    TestClass(int, std::shared_ptr<int>, int)
    {
        static_assert(CLASS_NAME == "ns::ns2::TestClass", "getClassName failed for class scope");
    }
    ~TestClass() { static_assert(CLASS_NAME == "ns::ns2::TestClass", "getClassName failed for class scope"); }
    void f(int, std::shared_ptr<int>)
    {
        static_assert(CLASS_NAME == "ns::ns2::TestClass", "getClassName failed for class scope");
        [[maybe_unused]] auto lambda = []() {
            static_assert(CLASS_NAME == "ns::ns2::TestClass", "getClassName failed for lambda scope");
        };
        fTemplated<int>(0, nullptr);
    }
    template<class T>
    void fTemplated(int, std::shared_ptr<T>)
    {
        static_assert(CLASS_NAME == "ns::ns2::TestClass", "getClassName failed for class scope");
    }
};
}  // namespace ns2
}  // namespace ns
#endif