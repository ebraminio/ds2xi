set_allowedplats("windows", "mingw")

target("main", function()
    set_kind("shared")
    set_languages("c++20")
    add_files("main.cpp", "xinput.def")
    add_syslinks("shlwapi", "kernel32", "winmm")
    set_runtimes("none")
    set_exceptions("none")
    set_basename("XInput1_4")
    set_prefixname("")
    set_warnings("allextra")
    set_optimize("faster")
    if is_plat("mingw") then
        add_cxxflags("-fno-stack-protector", "-fno-rtti", {force = true})
        local entry = is_arch("i386", "x86") and "_DllMain@12" or "DllMain"
        add_shflags("-nostartfiles", "-nodefaultlibs", "-Wl,--kill-at", "-Wl,--entry=" .. entry, {force = true})
    else
        add_cxflags("-GS-", {force = true})
        add_shflags("/ENTRY:DllMain", "/NODEFAULTLIB", "/SUBSYSTEM:WINDOWS", {force = true})
    end
    if is_mode("release") then
        set_strip("all")
    end
end)
