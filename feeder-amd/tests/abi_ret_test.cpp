#include <cstdint>
struct H { uint64_t handle; };
template <typename T> struct msvc_ret { T value; msvc_ret(const T &v) : value(v) {} msvc_ret(const msvc_ret &o) : value(o.value) {} };
struct C {
    virtual H plain(const char *a, const char *b) = 0;
    virtual msvc_ret<H> wrapped(const char *a, const char *b) = 0;
};
struct Impl : C {
    H plain(const char *a, const char *b) override { return H{ (uint64_t)a + (uint64_t)b }; }
    msvc_ret<H> wrapped(const char *a, const char *b) override { return H{ (uint64_t)a + (uint64_t)b }; }
};
Impl g;
extern "C" uint64_t call_plain(const char *a, const char *b) { return g.plain(a, b).handle; }
extern "C" uint64_t call_wrapped(const char *a, const char *b) { return g.wrapped(a, b).value.handle; }
