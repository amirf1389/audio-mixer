# AmF X1 IoT SDK as an ESP-IDF component. The protocol core is shared with the Arduino library (one source file for both).
set(AMFX1_CORE "${CMAKE_CURRENT_LIST_DIR}/../../arduino/AmFX1/src")
idf_component_register(SRCS "amfx1.c" "${AMFX1_CORE}/amfx1_core.c"
                       INCLUDE_DIRS "include" "${AMFX1_CORE}"
                       PRIV_REQUIRES lwip esp_hw_support)
