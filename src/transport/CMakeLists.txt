muckaiiLibrary(transport SOURCES socket.cpp telnet.cpp)
if(WIN32)
    target_link_libraries(muckaiiTransport PUBLIC ws2_32)
endif()

# Test support (docs/build.md "Targets"): the loopback listener the fake servers accept on.
add_library(muckaiiTransportLoopback STATIC loopback/tcpListener.cpp)
target_include_directories(muckaiiTransportLoopback PUBLIC loopback/include)
target_link_libraries(muckaiiTransportLoopback PUBLIC muckaiiTransport)
muckaiiTargetDefaults(muckaiiTransportLoopback)

muckaiiTests(transport SOURCES transportTests.cpp)
target_link_libraries(muckaiiTransportTests PRIVATE muckaiiTransportLoopback)
