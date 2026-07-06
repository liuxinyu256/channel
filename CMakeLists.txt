cmake_minimum_required(VERSION 3.14)
project(packetizer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================
#  静态库：封包器核心（不含硬件依赖，可独立测试）
# ============================================================
add_library(packetizer_core STATIC
    src/packet.c
    src/packetizer_timeout.c
)
target_include_directories(packetizer_core PUBLIC
    ${CMAKE_SOURCE_DIR}       # packetizer.h
    ${CMAKE_SOURCE_DIR}/src   # 内部头文件
)

# ============================================================
#  静态库：硬件定时器（平台相关，测试时用 mock 替代）
# ============================================================
add_library(packetizer_hw STATIC
    src/frame_timer_hw.c
    src/timer1.c
)
target_include_directories(packetizer_hw PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(packetizer_hw PUBLIC packetizer_core)

# ============================================================
#  示例程序
# ============================================================
add_executable(main main.c)
target_link_libraries(main PRIVATE packetizer_core packetizer_hw)

# ============================================================
#  单元测试（只链核心库，硬件定时器用 mock）
# ============================================================
add_executable(test_packetizer test/test_packetizer.c)
target_link_libraries(test_packetizer PRIVATE packetizer_core)

enable_testing()
add_test(NAME packetizer_test COMMAND test_packetizer)
