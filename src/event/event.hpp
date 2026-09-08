#ifndef INCLUDE_TKW_EVENT_EVENT_HPP
#define INCLUDE_TKW_EVENT_EVENT_HPP

#include <cstdint>
#include <string_view>

#include "util/marco.hpp"

namespace tkw
{
    template <typename T>
    class CommonEventBus;

    class Event
    {
        friend class CommonEventBus<Event>;
        DEFINE_ATTRIBUTE(uint64_t, sequence);

    private:
        void set_sequence(uint64_t seq) noexcept { sequence = seq; }

    public:
        Event() : sequence(0) {}

        virtual ~Event() = default;
        virtual std::string_view type_tag() const noexcept { return "Event"; }
    };
}

#endif  // INCLUDE_TKW_EVENT_EVENT_HPP
