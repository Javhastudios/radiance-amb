cmake_minimum_required(VERSION 3.22)

# Versión mínima de macOS (High Sierra). Tiene que ir ANTES de project() para que se aplique.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Versión mínima de macOS")

project(RadianceAMB VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: el runtime de C++ va dentro del ejecutable (no hace falta instalar Visual C++ Redistributable)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE se descarga automáticamente al configurar (no hace falta instalarlo).
# Para usar una copia local: -DFETCHCONTENT_SOURCE_DIR_JUCE=/ruta/a/JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(RA_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND RA_FORMATS AU)
endif()

juce_add_plugin(RadianceAMB
    PRODUCT_NAME "Radiance-AMB"
    COMPANY_NAME "Javhastudios"
    BUNDLE_ID "com.javhastudios.radianceamb"
    PLUGIN_MANUFACTURER_CODE Javh
    PLUGIN_CODE RaAm
    FORMATS ${RA_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_add_binary_data(RadianceData SOURCES
    Resources/Raleway-500.ttf
    Resources/Raleway-600.ttf
    Resources/Raleway-700.ttf
    Resources/JetBrainsMono-400.ttf
    Resources/JetBrainsMono-600.ttf
    Resources/Audiowide.ttf
    Resources/logo_javha.png
    Resources/logo_mark.png)

set(RA_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp
    Source/ui/Views.cpp)

target_sources(RadianceAMB PRIVATE ${RA_SOURCES})
target_include_directories(RadianceAMB PRIVATE Source)

# Los textos de la interfaz llevan acentos y símbolos: en Windows hay que compilar como UTF-8
if(MSVC)
    target_compile_options(RadianceAMB PRIVATE /utf-8)
endif()

target_compile_definitions(RadianceAMB PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(RadianceAMB
    PRIVATE
        RadianceData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Comprobación del motor contra el prototipo web (no necesita JUCE)
add_executable(compare_cpp EXCLUDE_FROM_ALL tests/compare_cpp.cpp)

# Prueba de humo del plugin completo (no se compila por defecto):
#   cmake --build build --target ra_smoke && ./build/ra_smoke_artefacts/Release/ra_smoke salida/
juce_add_console_app(ra_smoke PRODUCT_NAME "ra_smoke")
set_target_properties(ra_smoke PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(ra_smoke PRIVATE tests/smoke.cpp ${RA_SOURCES})
target_include_directories(ra_smoke PRIVATE Source)
target_compile_definitions(ra_smoke PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(ra_smoke PRIVATE RadianceData juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
