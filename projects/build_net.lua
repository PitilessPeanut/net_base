-- Invoke: ./premake5 --file=../premake5.lua --os=ios xcode4

protect_strings = "DO_PROTECT_STRINGS=1"
remove_strings =  "DO_REMOVE_STRINGS=0"

workspace "net_base"
        configurations { "Debug", "Release" }
        architecture "x86_64"
        project "net_base"
                language "c++"
                kind "ConsoleApp" -- "WindowedApp"
                cppdialect "C++20"
                toolset "gcc" --"clang"
                rtti "Off"
                exceptionhandling "On"
                warnings "extra"
                files { "../src_net/*.cpp",
                        "../src_net/*.hpp"
                      }
                targetdir "../"
                defines { "BOOST_ASIO_ENABLE_HANDLER_TRACKING,"
                	      "BOOST_ASIO_ENABLE_BUFFER_DEBUGGING"
                        }






                filter { "system:windows", "action:vs*" }
                        --systemversion(os.winSdkVersion() .. ".0")
                        buildoptions { "/fp:fast" }
                        defines { "_CRT_SECURE_NO_WARNINGS",
                                  "_CRT_NONSTDC_NO_WARNINGS",
                                  protect_strings,
                                  remove_strings
                                }
                        files { 
                              }
                        location "../build_win"






                gcc_buildoption_ignoreNaN = "-ffinite-math-only"
                gcc_buildoption_addAddressSanitize = "-fsanitize=address" -- dynamic bounds check "undefined reference"
                gcc_buildoption_utf8compiler = "-finput-charset=UTF-8 -fexec-charset=UTF-8 -fextended-identifiers"
                gcc_buildoption_fatal = "-Wfatal-errors" -- make gcc output bearable
                gcc_buildoption_shadow = "-Wshadow-compatible-local"
                gcc_buildoption_impl_fallthrough = "-Wimplicit-fallthrough" -- warn missing [[fallthrough]]
                gcc_buildoption_undef = "-Wundef" -- Macros must be defined

                filter { "system:ios" }
                        toolset "clang"
                        buildoptions { "-ffast-math"
                                     , "-pedantic"
                                     , gcc_buildoption_fatal
                                     , gcc_buildoption_shadow
                                     }
                        defines { protect_strings,
                                  remove_strings
                                }
                        files { 
                              }
                        removefiles { "../src_net/.DS_Store",
                                      "../src_net/Assets.xcassets/.DS_Store"
                                    }
                        location "../build_ios"

                filter {} -- "deactivate"






                filter { "system:linux" }
                        toolset "gcc"
                        buildoptions { "-march=native"
                                     , "-pedantic"
                                     , "-ffast-math"
                                     , gcc_buildoption_utf8compiler
                                     , gcc_buildoption_fatal
                                     , gcc_buildoption_shadow
                                     , gcc_buildoption_impl_fallthrough
                                     , gcc_buildoption_undef
                                     }
                        defines { protect_strings,
                                  remove_strings
                                }
                        location "../build_linux"
                        links { "pthread",
                                "crypto",
                                "ssl"
                                -- "dl"
                              }
                        optimize "Speed" --"Size" --Debug" "Full"

                filter {} -- "deactivate"





                filter "configurations:Debug"
                        symbols "On"
                        defines "_DEBUG"
                        optimize "Speed"

                filter "configurations:Release"
                        symbols "Off"
                        defines "NDEBUG"
                        optimize "Speed"

                filter {}

print("done")
os.remove("Makefile")
