# main component — motor-side ESP32-C3
#
# The physical-output source (ledc_ppm_output.cpp) is only compiled when
# MOTOR_OUTPUT_ENABLED is 1, so the bench-safe build cannot accidentally pull in
# the pin-driving code path.

set(srcs
    "main.cpp"
    "power_curve.cpp"
    "throttle_supervisor.cpp"
    "ble_central.cpp"
)

# Default bench-safe (0). Override at build time with:
#   idf.py -DMOTOR_OUTPUT_ENABLED=1 build      (technician sign-off only)
if(NOT DEFINED MOTOR_OUTPUT_ENABLED)
    set(MOTOR_OUTPUT_ENABLED 0)
endif()

if(MOTOR_OUTPUT_ENABLED)
    list(APPEND srcs "ledc_ppm_output.cpp")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES
        bt              # NimBLE
        driver          # LEDC (only used when output enabled)
        nvs_flash       # bonding key storage
        esp_timer
)

# Propagate the flag into the C/C++ preprocessor.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MOTOR_OUTPUT_ENABLED=${MOTOR_OUTPUT_ENABLED})

# C++17 to match the existing firmware architecture.
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
