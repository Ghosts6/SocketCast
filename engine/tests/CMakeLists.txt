add_executable(test_packet test_packet.cpp)
target_link_libraries(test_packet PRIVATE socketcast_core)
add_test(NAME test_packet COMMAND test_packet)

add_executable(test_rto test_rto.cpp)
target_link_libraries(test_rto PRIVATE socketcast_core)
add_test(NAME test_rto COMMAND test_rto)

add_executable(test_session test_session.cpp)
target_link_libraries(test_session PRIVATE socketcast_core)
add_test(NAME test_session COMMAND test_session)

add_executable(test_loopback test_loopback.cpp)
target_link_libraries(test_loopback PRIVATE socketcast_core Threads::Threads)
add_test(NAME test_loopback COMMAND test_loopback)

add_executable(test_cli test_cli.cpp)
target_link_libraries(test_cli PRIVATE socketcast_core)
add_test(NAME test_cli COMMAND test_cli $<TARGET_FILE:socketcast_engine>)

add_executable(test_jitter_buffer test_jitter_buffer.cpp)
target_link_libraries(test_jitter_buffer PRIVATE socketcast_core)
add_test(NAME test_jitter_buffer COMMAND test_jitter_buffer)

add_executable(test_rate_controller test_rate_controller.cpp)
target_link_libraries(test_rate_controller PRIVATE socketcast_core)
add_test(NAME test_rate_controller COMMAND test_rate_controller)
