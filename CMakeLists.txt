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


# ============================================================
#  软件定时器库（PC 端模拟，无硬件依赖）
# ============================================================
add_library(packetizer_sw STATIC
    src/frame_timer_sw.c
    src/log.c
)
target_include_directories(packetizer_sw PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(packetizer_sw PUBLIC packetizer_core)

# ============================================================
#  交互式模拟器
# ============================================================
add_executable(sim_app sim/sim_app.c)
target_link_libraries(sim_app PRIVATE packetizer_core packetizer_sw)

# ============================================================
#  GUI 模拟器（Windows 原生窗口）
# ============================================================
add_executable(sim_gui sim/sim_gui.c)
target_link_libraries(sim_gui PRIVATE packetizer_core packetizer_sw gdi32)

# ============================================================
#  批量压力测试
# ============================================================
add_executable(test_stress test/test_stress_script.c)
target_link_libraries(test_stress PRIVATE packetizer_core packetizer_sw)

# ============================================================
#  测试
# ============================================================
enable_testing()
add_test(NAME packetizer_test COMMAND test_packetizer)
add_test(NAME stress_quick   COMMAND test_stress -n 1000 -l 64 -q)
add_test(NAME stress_255     COMMAND test_stress -n 500  -l 255 -q)
add_test(NAME stress_gap     COMMAND test_stress -n 500  -l 128 -g 1 -q)
