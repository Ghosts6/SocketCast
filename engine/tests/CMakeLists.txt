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

add_executable(test_nal_parser test_nal_parser.cpp)
target_link_libraries(test_nal_parser PRIVATE socketcast_core)
add_test(NAME test_nal_parser COMMAND test_nal_parser)

add_executable(test_media_source test_media_source.cpp)
target_link_libraries(test_media_source PRIVATE socketcast_core)
add_test(NAME test_media_source COMMAND test_media_source)

add_executable(test_media_session test_media_session.cpp)
target_link_libraries(test_media_session PRIVATE socketcast_core)
add_test(NAME test_media_session COMMAND test_media_session)

add_executable(test_media_sink test_media_sink.cpp)
target_link_libraries(test_media_sink PRIVATE socketcast_core)
add_test(NAME test_media_sink COMMAND test_media_sink)

add_executable(test_admin_server test_admin_server.cpp)
target_link_libraries(test_admin_server PRIVATE socketcast_core Threads::Threads)
add_test(NAME test_admin_server COMMAND test_admin_server)

add_executable(test_audio_buffer test_audio_buffer.cpp)
target_link_libraries(test_audio_buffer PRIVATE socketcast_core)
add_test(NAME test_audio_buffer COMMAND test_audio_buffer)
