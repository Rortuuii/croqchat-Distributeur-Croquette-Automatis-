idf_component_register(
    SRCS
        "main.c"
        "common/storage.c"
        "common/app_controller.c"
        "protocol/ble_protocol.c"
        "fb01_weight/weight_manager.c"
        "fb02_motor/motor_controller.c"
        "fb03_dispenser/dispenser_controller.c"
        "fb04_safety/safety_manager.c"
        "fb05_scheduler/meal_scheduler.c"
        "fb06_ble/ble_manager.c"
        "fb06_ble/ble_commands.c"
        "cat_identification/cat_identification.c"
        "cat_identification/manual_badge.c"
    INCLUDE_DIRS
        "../include" "common" "protocol"
        "fb01_weight" "fb02_motor" "fb03_dispenser"
        "fb04_safety" "fb05_scheduler" "fb06_ble" "cat_identification"
    REQUIRES bt nvs_flash esp_driver_gpio esp_driver_uart esp_timer
)
