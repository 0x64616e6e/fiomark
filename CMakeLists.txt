cmake_minimum_required(VERSION 3.21)
project(fiomark VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui DBus Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(fiomark main.cpp)
qt_add_qml_module(fiomark
    URI FioMark
    VERSION 1.0
    QML_FILES Main.qml
    SOURCES fiorunner.h fiorunner.cpp smartinfo.h smartinfo.cpp
)
target_link_libraries(fiomark PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2)
target_compile_options(fiomark PRIVATE -Wall -Wextra)

# smoke tests: no display and no disk I/O needed, so they are safe on build daemons
enable_testing()
add_test(NAME cli-help COMMAND fiomark --help)
set_tests_properties(cli-help PROPERTIES PASS_REGULAR_EXPRESSION "--runtime" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME cli-rejects-bad-dir COMMAND fiomark --cli --dir /nonexistent-fiomark-test)
set_tests_properties(cli-rejects-bad-dir PROPERTIES WILL_FAIL TRUE ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

include(GNUInstallDirs)
install(TARGETS fiomark RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES fiomark.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES fiomark.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
