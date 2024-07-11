cmake_minimum_required(VERSION 3.16)

find_package(mo2-cmake CONFIG REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS graph assign)

add_library(diagnose_basic SHARED)
mo2_configure_plugin(diagnose_basic WARNINGS OFF)
target_link_libraries(diagnose_basic PRIVATE mo2::uibase Boost::assign Boost::graph)
mo2_install_plugin(diagnose_basic)
