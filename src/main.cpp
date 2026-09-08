#include <iostream>
#include <string>

#include <pjh_json.hpp>
#include <pjh_platform.hpp>
#include <pjh_result.hpp>

int main()
{
    auto result =
        pjh::result::Result<std::string, std::nullopt_t>::Ok("Hello Three Kingdoms!");
    std::cout << result.unwrap() << std::endl;

    pjh::json::Json json("Hello World from PJH_JSON!");
    std::cout << json.as_string() << std::endl;

    pjh::platform::FileEventKind::Created;
    std::cout << "Hello World!" << std::endl;
}
