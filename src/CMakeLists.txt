cmake_minimum_required(VERSION 3.16)

add_library(diagnose_basic SHARED)
mo2_configure_plugin(diagnose_basic WARNINGS OFF PRIVATE_DEPENDS boost)
mo2_install_target(diagnose_basic)
