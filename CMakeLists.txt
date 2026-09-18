cmake_minimum_required(VERSION 3.23)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MULTISENDER_VERSION)
string(STRIP "${MULTISENDER_VERSION}" MULTISENDER_VERSION)
if(NOT MULTISENDER_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
    message(FATAL_ERROR "VERSION must contain MAJOR.MINOR.PATCH, for example 2.1.0")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

project(MultiSender VERSION "${MULTISENDER_VERSION}" LANGUAGES CXX)

configure_file(src/appversion.h.in generated/appversion.h @ONLY)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_TESTING "Build automated tests" OFF)

set(MULTISENDER_PACKAGE_DIRECTORY
    "${CMAKE_SOURCE_DIR}/build_agent/package"
    CACHE PATH "Directory containing the runnable MultiSender package"
)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Network SerialPort Widgets)
qt_standard_project_setup(REQUIRES 6.11)

qt_add_library(MultiSenderCore STATIC
    src/payloadencoding.cpp
    src/payloadencoding.h
    src/requestcontroller.cpp
    src/requestcontroller.h
    src/requestfilterproxymodel.cpp
    src/requestfilterproxymodel.h
    src/requestlogmodel.cpp
    src/requestlogmodel.h
    src/requesttypes.cpp
    src/requesttypes.h
    src/settingsstore.cpp
    src/settingsstore.h
    src/uilanguage.cpp
    src/uilanguage.h
    src/workspacestore.cpp
    src/workspacestore.h
    src/workspaceexchange.cpp
    src/workspaceexchange.h
)

target_include_directories(MultiSenderCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(MultiSenderCore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::SerialPort
)

qt_add_executable(MultiSender WIN32
    src/apptheme.cpp
    src/apptheme.h
    src/codeeditor.cpp
    src/codeeditor.h
    src/collectionrequestdelegate.cpp
    src/collectionrequestdelegate.h
    src/logpane.cpp
    src/logpane.h
    src/responseviewer.cpp
    src/responseviewer.h
    src/httpheaderseditor.cpp
    src/httpheaderseditor.h
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/payloadhighlighter.cpp
    src/payloadhighlighter.h
)
target_link_libraries(MultiSender PRIVATE MultiSenderCore Qt6::Widgets)
qt_add_resources(MultiSender "ui_translations"
    PREFIX "/i18n"
    BASE "src/i18n"
    FILES src/i18n/en.json src/i18n/es.json src/i18n/pt.json src/i18n/tr.json src/i18n/ar.json
)
qt_add_resources(MultiSender "app_icons"
    PREFIX "/"
    FILES Installer/MultiSender.ico
          Installer/check-dark.svg Installer/check-light.svg
          Installer/chevron-dark.svg Installer/chevron-light.svg
          Installer/spin-up-dark.svg Installer/spin-down-dark.svg
          Installer/spin-up-light.svg Installer/spin-down-light.svg
)

if(WIN32)
    foreach(component MAJOR MINOR PATCH)
        if(PROJECT_VERSION_${component} GREATER 65535)
            message(FATAL_ERROR "Windows executable version components must not exceed 65535")
        endif()
    endforeach()
endif()

target_compile_definitions(MultiSenderCore PRIVATE QT_DEPRECATED_WARNINGS)
target_include_directories(MultiSender PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(MultiSender PRIVATE QT_DEPRECATED_WARNINGS)
set_target_properties(MultiSender PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${MULTISENDER_PACKAGE_DIRECTORY}"
    WIN32_EXECUTABLE TRUE
)

foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(MultiSender PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
        "${MULTISENDER_PACKAGE_DIRECTORY}"
    )
endforeach()

if(WIN32)
    set(MULTISENDER_EXECUTABLE_NAME "MultiSender")
    set(MULTISENDER_FILE_DESCRIPTION "MultiSender")
    configure_file(Installer/MultiSender.rc.in generated/MultiSender.rc @ONLY)
    target_sources(MultiSender PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/MultiSender.rc")
endif()

install(TARGETS MultiSender
    RUNTIME DESTINATION .
)
install(FILES VERSION CHANGELOG.md DESTINATION .)
