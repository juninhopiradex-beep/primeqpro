cmake_minimum_required(VERSION 3.22)

# macOS: um unico binario universal (arm64 + x86_64) com minimo 10.13 -> corre no MacBook Pro 2013 (Catalina 10.15.7, Intel)
# e em qualquer Mac recente (Apple Silicon, macOS 14/15/26).
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS minimo")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Arquiteturas")
endif()
# Windows: runtime do Visual C++ embutido (nao precisa do VC++ Redistributable)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(PRIMEQPRO VERSION 1.6.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8.0.15 (GPLv3 / licenca comercial JUCE — ver Docs/LICENSES.md)
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# Formatos. AAX so existe quando o SDK da Avid esta disponivel (-DAAX_SDK_PATH=...) e o binario e depois assinado com PACE/iLok.
if(APPLE)
    set(PQ_FORMATS VST3 AU Standalone)
else()
    set(PQ_FORMATS VST3 Standalone)
endif()
set(PQ_AAX OFF)
if(DEFINED AAX_SDK_PATH AND EXISTS "${AAX_SDK_PATH}")
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    list(APPEND PQ_FORMATS AAX)
    set(PQ_AAX ON)
    message(STATUS "AAX ativado (SDK em ${AAX_SDK_PATH})")
else()
    message(STATUS "AAX desativado: defina -DAAX_SDK_PATH=<pasta do AAX SDK da Avid>")
endif()

juce_add_plugin(PrimeqPro
    COMPANY_NAME "Piradex"
    BUNDLE_ID "com.piradex.primeqpro"
    PLUGIN_MANUFACTURER_CODE Prdx
    PLUGIN_CODE PxPQ
    FORMATS ${PQ_FORMATS}
    PRODUCT_NAME "PRIMEQ PRO by Piradex"
    DESCRIPTION "PRIMEQ PRO - equalizador parametrico e dinamico (24 bandas)"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect
    AAX_CATEGORY AAX_ePlugInCategory_EQ
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(PrimeqPro PRIVATE
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/Access.cpp
    Source/GUI/PluginEditor.cpp)
target_include_directories(PrimeqPro PRIVATE Source)

target_compile_definitions(PrimeqPro PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0)
# PCC (Plugin Control Central): ligado so se URL e chave publica Ed25519 (64 hex) forem dadas
set(PQ_PCC_URL "https://ejjeittwyjttmevhajxo.supabase.co" CACHE STRING "URL do projeto Supabase do PCC (https://xxxx.supabase.co)")
set(PQ_PCC_PUBKEY "064c37512051906b9c8f1a9dfc962e9a1cd4c9f5b0f44ce66a12842b8e70a868" CACHE STRING "Chave publica Ed25519 do PCC (64 hex)")
if(PQ_PCC_URL AND PQ_PCC_PUBKEY)
    target_compile_definitions(PrimeqPro PUBLIC PQ_PCC_URL="${PQ_PCC_URL}" PQ_PCC_PUBKEY="${PQ_PCC_PUBKEY}")
endif()
if(PQ_AAX)
    target_compile_definitions(PrimeqPro PUBLIC PQ_AAX=1)
endif()
if(MSVC)
    target_compile_options(PrimeqPro PRIVATE /utf-8 /bigobj)
endif()

target_link_libraries(PrimeqPro
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags)
option(PQ_ENABLE_LTO "Otimizacao em tempo de ligacao (CI)" ON)
if(PQ_ENABLE_LTO)
    target_link_libraries(PrimeqPro PUBLIC juce::juce_recommended_lto_flags)
endif()

# ---- testes DSP (C++ puro, sem JUCE) -----------------------------------------------------------
option(PQ_BUILD_TESTS "Compilar os testes DSP" OFF)
if(PQ_BUILD_TESTS)
    add_executable(test_dsp Tests/DSP/test_dsp.cpp)
    target_include_directories(test_dsp PRIVATE Source)
    enable_testing()
    add_test(NAME dsp COMMAND test_dsp)
endif()

# ---- teste de integracao do processador (JUCE, sem anfitriao) ------------------------------------
option(PQ_BUILD_SMOKE "Compilar o teste de integracao do processador" OFF)
if(PQ_BUILD_SMOKE)
    juce_add_console_app(PrimeqSmoke PRODUCT_NAME "primeq_smoke")
    target_sources(PrimeqSmoke PRIVATE Tests/Integration/smoke.cpp Source/Plugin/PluginProcessor.cpp Source/Plugin/Access.cpp Source/GUI/PluginEditor.cpp)
    target_include_directories(PrimeqSmoke PRIVATE Source)
    target_compile_definitions(PrimeqSmoke PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_STANDALONE_APPLICATION=1
        JucePlugin_VersionString="${PROJECT_VERSION}" JucePlugin_Name="PRIMEQ PRO by Piradex" JUCE_PLUGINHOST_AU=0)
    target_link_libraries(PrimeqSmoke PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_recommended_config_flags)
endif()
