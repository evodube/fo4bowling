-- fo4bowling: playable bowling at Back Alley Bowling for Fallout 4 (F4SE plugin).
-- Built against Dear-Modding CommonLibF4. Primary target: Fallout 4 OG (1.10.163).
includes("lib/commonlibf4")

set_project("fo4bowling")
set_version("0.2.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")
set_allowedarchs("windows|x64")
set_allowedmodes("debug", "releasedbg")
set_defaultarchs("windows|x64")
set_defaultmode("releasedbg")

add_rules("mode.debug", "mode.releasedbg")

-- REL::ID / REL::Offset initializer slots are [OG, NG, AE].
add_defines("COMMONLIB_RUNTIMECOUNT=3")

target("fo4bowling", function()
    add_rules("commonlibf4.plugin", {
        name = "fo4bowling",
        author = "evodube",
        description = "Playable bowling at Back Alley Bowling.",
        plugin_template = path.join(os.projectdir(), "res/commonlibf4-plugin.cpp.in"),
    })

    -- don't run `xmake install` after every build (CI has no game folder)
    set_values("commonlib.plugin.install", false)

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")

    add_defines(
        "_UNICODE",
        "UNICODE",
        "NOMINMAX",
        "_CRT_SECURE_NO_WARNINGS",
        "_WIN32_WINNT=0x0A00",
        "WINVER=0x0A00",
        "_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING"
    )

    add_cxxflags(
        "/Zi",
        "/permissive-",
        "/Zc:preprocessor",
        "/EHsc",
        "/wd4100", -- unreferenced parameter
        "/wd4189", -- local variable initialized but not referenced
        "/wd4244", -- conversion, possible loss of data
        "/wd4267", -- size_t conversion
        "/wd5054"  -- enum bitwise ops
    )

    add_syslinks("d3d11", "dxgi", "user32", "kernel32", "ole32", "xaudio2")

    set_pcxxheader("src/PCH.h")
    set_runtimes("MD")
    set_symbols("debug")
    set_optimize("fastest")
    set_targetdir("build/out/F4SE/Plugins")
end)
