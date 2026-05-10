add_executable(TestsSocksServer "TestsSocksServer.cpp" )
set_property(TARGET TestsSocksServer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(WIN32)
	target_link_libraries(TestsSocksServer SocksServer)
else()
	target_link_libraries(TestsSocksServer SocksServer pthread)
endif()

if(DEFINED C2_TEST_BIN_OUTPUT_DIR)
	add_custom_command(TARGET TestsSocksServer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy
		$<TARGET_FILE:TestsSocksServer> "${C2_TEST_BIN_OUTPUT_DIR}/$<TARGET_FILE_NAME:TestsSocksServer>")
endif()
