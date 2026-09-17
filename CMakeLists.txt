cmake_minimum_required(VERSION 3.21)
project(fiomark VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(fiomark main.cpp)
qt_add_qml_module(fiomark
    URI FioMark
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES fiorunner.h fiorunner.cpp
)
target_link_libraries(fiomark PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
target_compile_options(fiomark PRIVATE -Wall -Wextra)

include(GNUInstallDirs)
install(TARGETS fiomark RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES fiomark.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
