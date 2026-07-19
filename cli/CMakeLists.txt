add_executable(pathguardctl src/main.cpp)
target_link_libraries(pathguardctl PRIVATE pathguard_core)
