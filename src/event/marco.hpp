#ifndef INCLUDE_TKW_EVENT_MARCO_HPP
#define INCLUDE_TKW_EVENT_MARCO_HPP

#define DEFINE_EVENT_START(name, parent) \
    class name##Event : public parent    \
    {

#define DEFINE_EVENT_END(name)                                                   \
public:                                                                          \
    std::string_view type_tag() const noexcept override { return #name "Event"; } \
    }                                                                            \
    ;

#endif  // INCLUDE_TKW_EVENT_MARCO_HPP
