add_executable(llama-ontogenesis ontogenesis-demo.cpp)
target_link_libraries(llama-ontogenesis PRIVATE llama ${CMAKE_THREAD_LIBS_INIT})
target_compile_features(llama-ontogenesis PRIVATE cxx_std_17)
install(TARGETS llama-ontogenesis RUNTIME)
