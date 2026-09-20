set_allowedplats("windows")

target("ds2xi", function()
    set_kind("binary")
    set_languages("c++20")
    add_includedirs("hidapi/hidapi", "ViGEmClient/include")
    add_files("main.cpp", "hidapi/windows/hid.c", "ViGEmClient/src/ViGEmClient.cpp")
    add_syslinks("user32", "kernel32", "shell32", "advapi32", "dwmapi", "setupapi")
    set_runtimes("MT")

    if is_mode("release") and is_plat("windows") then
        add_ldflags("-subsystem:windows", {force = true})
        add_ldflags("-entry:mainCRTStartup", {force = true}) -- Ensures it still looks for main()
    end
end)
