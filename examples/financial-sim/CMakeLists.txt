set(TARGET llama-financial-sim)
add_executable(${TARGET} financial-sim.cpp)
install(TARGETS ${TARGET} RUNTIME)
target_link_libraries(${TARGET} PRIVATE common llama ${CMAKE_THREAD_LIBS_INIT})
target_compile_features(${TARGET} PRIVATE cxx_std_17)

# Virtual Device Admin Utility (standalone, no model required)
set(ADMIN_TARGET llama-device-admin)
add_executable(${ADMIN_TARGET} 
    device-admin.cpp 
    virtual-device.cpp 
    financial-device-driver.cpp
)
install(TARGETS ${ADMIN_TARGET} RUNTIME)
target_link_libraries(${ADMIN_TARGET} PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_compile_features(${ADMIN_TARGET} PRIVATE cxx_std_17)

# Virtual Device Demo (standalone, no model required)
set(DEMO_TARGET demo-virtual-device)
add_executable(${DEMO_TARGET}
    demo-virtual-device.cpp
    virtual-device.cpp
    financial-device-driver.cpp
)
target_link_libraries(${DEMO_TARGET} PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_compile_features(${DEMO_TARGET} PRIVATE cxx_std_17)

# Test for virtual device (no model required)
set(VDEV_TEST_TARGET test-virtual-device)
add_executable(${VDEV_TEST_TARGET}
    test-virtual-device.cpp
    virtual-device.cpp
    financial-device-driver.cpp
)
target_link_libraries(${VDEV_TEST_TARGET} PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_compile_features(${VDEV_TEST_TARGET} PRIVATE cxx_std_17)

# Test for financial logic (no model required)
set(TEST_TARGET test-financial-logic)
add_executable(${TEST_TARGET} test-financial-logic.cpp)
target_compile_features(${TEST_TARGET} PRIVATE cxx_std_17)

# Test for advanced transaction engine (no model required)
set(TX_ENGINE_TEST_TARGET test-transaction-engine)
add_executable(${TX_ENGINE_TEST_TARGET}
    test-transaction-engine.cpp
    transaction-engine.cpp
)
target_link_libraries(${TX_ENGINE_TEST_TARGET} PRIVATE ${CMAKE_THREAD_LIBS_INIT})
target_compile_features(${TX_ENGINE_TEST_TARGET} PRIVATE cxx_std_17)