cmake_minimum_required(VERSION 3.0)


set(LINKER_FLAGS "-T ${CMAKE_CURRENT_SOURCE_DIR}/stm32.ld -Wl,--gc-sections --static -nostartfiles -specs=nano.specs -specs=nosys.specs")
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "${LINKER_FLAGS}")

include_directories(libopencm3/include/)
link_directories(libopencm3/lib/)

if (1)
    add_executable(controller.elf main.c stm32f103.spi.c steps.c uart.c)
else()
    add_executable(controller.elf main.c stm32f103.eth.c steps.c uart.c)
endif()

target_link_libraries(controller.elf control err gcode output opencm3_stm32f1 m enc28j60)
target_include_directories(controller.elf PUBLIC enc28j60)

add_custom_target(controller.bin ALL arm-none-eabi-objcopy -O binary controller.elf controller.bin)
add_dependencies(controller.bin controller.elf)

