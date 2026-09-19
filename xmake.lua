set_allowedplats("windows")

add_requires("hidapi")

target("ds2xi")
    set_languages("c++20")
    set_kind("binary")
    add_includedirs("hidapi/hidapi", "ViGEmClient/include")
    add_files("main.cpp", "hidapi/windows/hid.c", "ViGEmClient/src/*.cpp")
    add_syslinks("user32", "kernel32", "shell32", "setupapi")
    set_runtimes("MT")
