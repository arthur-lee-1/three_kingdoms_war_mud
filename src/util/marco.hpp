#ifndef INCLUDE_TKW_UTIL_MARCO_HPP
#define INCLUDE_TKW_UTIL_MARCO_HPP

#define DEFAULT_CONSTRUCTOR(name)            \
    name() = default;                        \
    name(const name &) = default;            \
    name(name &&) noexcept = default;        \
    name &operator=(const name &) = default; \
    name &operator=(name &&) noexcept = default;


#define DEFINE_ATTRIBUTE(type, name)             \
private:                                         \
    type name;                                   \
                                                 \
public:                                          \
    type &get_##name() noexcept { return name; } \
    const type &get_##name() const noexcept { return name; }

#define DEFINE_DEFAULT_VALUE_ATTRIBUTE(type, name, val) \
private:                                                \
    type name = val;                                    \
                                                        \
public:                                                 \
    type &get_##name() noexcept { return name; }        \
    const type &get_##name() const noexcept { return name; }

#define CLASS_BUILDER_START(name) \
    class name##Builder           \
    {                             \
        friend name;              \
                                  \
    private:                      \
        name data;                \
                                  \
    private:                      \
        name##Builder() {}        \
                                  \
    public:                       \
        name build() { return data; }

#define CLASS_BUILDER_ATTRIBUTE(name, type, attr) \
public:                                           \
    name##Builder &attr(type val)                 \
    {                                             \
        data.get_##attr().set_tot(val);           \
        data.get_##attr().set_cur(val);           \
        return *this;                             \
    }

#define CLASS_BUILDER_END(name) \
    }                           \
    ;                           \
    inline name##Builder name::create() { return {}; }

#endif  // INCLUDE_TKW_UTIL_MARCO_HPP
