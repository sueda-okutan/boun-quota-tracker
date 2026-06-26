find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(test_parser
    test_parser.cpp
)

target_link_libraries(test_parser
    PRIVATE
        boun_quota_core
        Qt6::Core
        Qt6::Network
        Qt6::Test
)

add_test(NAME test_parser COMMAND test_parser)
