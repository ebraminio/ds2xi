set_allowedplats("windows")

target("xinput_proxy", function()
    set_kind("shared")
    set_languages("c++20")
    add_includedirs("hidapi/hidapi")
    add_files("main.cpp", "hidapi/windows/hid.c")
    add_syslinks("user32", "kernel32", "shlwapi")
    set_runtimes("MT")

    -- Output as XInput1_3.dll so games load it instead of the system DLL
    set_basename("XInput1_3")
    set_prefixname("")

    if is_mode("release") and is_plat("windows") then
        add_ldflags("-subsystem:windows", {force = true})
    end
end)
