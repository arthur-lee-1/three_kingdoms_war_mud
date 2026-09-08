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

#endif  // INCLUDE_TKW_UTIL_MARCO_HPP
