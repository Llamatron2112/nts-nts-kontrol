cmake_minimum_required(VERSION 3.20)
project(nts-nts-kontrol VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent Test)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(JACK QUIET IMPORTED_TARGET jack)
endif()
option(BUILD_UI "Build the Qt GUI" ON)

add_library(miniz STATIC third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
target_include_directories(miniz PUBLIC third_party/miniz)

add_library(korgcore STATIC
    src/core/protocol/KorgProtocol.cpp
    src/core/midi/MidiBackend.cpp
    src/core/midi/SysExAssembler.cpp
    src/core/midi/RawMidiDevice.cpp
    src/core/model/Program.cpp
    src/core/model/UserUnit.cpp
    src/core/model/GlobalData.cpp
    src/core/model/Workspace.cpp
    src/core/file/WorkspaceFile.cpp
    src/core/DeviceSession.cpp
)
target_include_directories(korgcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(korgcore PUBLIC Qt6::Core miniz)
if(JACK_FOUND)
    target_sources(korgcore PRIVATE src/core/midi/JackMidiDevice.cpp)
    target_compile_definitions(korgcore PRIVATE HAVE_JACK=1)
    target_link_libraries(korgcore PUBLIC PkgConfig::JACK)
    message(STATUS "Building PipeWire/JACK MIDI backend")
else()
    message(STATUS "JACK not found; building only the raw ALSA MIDI backend")
endif()

add_executable(nts-nts-kontrol-cli
    src/cli/main.cpp
)
target_link_libraries(nts-nts-kontrol-cli PRIVATE korgcore)

if(BUILD_UI)
    add_executable(nts-nts-kontrol
        src/main.cpp
        src/ui/MainWindow.cpp
        src/ui/ProgramPage.cpp
        src/ui/UnitPage.cpp
        src/ui/GlobalPage.cpp
        src/ui/PreferencesDialog.cpp
        resources/resources.qrc
    )
    target_link_libraries(nts-nts-kontrol PRIVATE korgcore Qt6::Widgets Qt6::Concurrent)
    install(TARGETS nts-nts-kontrol RUNTIME DESTINATION bin)
endif()

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE korgcore Qt6::Test)
add_test(NAME core COMMAND test_core)

if(BUILD_UI)
    install(FILES resources/nts-nts-kontrol.desktop DESTINATION share/applications)
    install(FILES resources/nts-nts-kontrol.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()
