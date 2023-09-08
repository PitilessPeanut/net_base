-- Invoke: ./premake5 --file=../premake5.lua --os=ios xcode4

protect_strings = "DO_PROTECT_STRINGS=1"
remove_strings =  "DO_REMOVE_STRINGS=0"

nix_buildoptions = { "-Wfatal-errors", -- make gcc output bearable
                     "-pedantic"
                   }

workspace "Gaem"
        configurations { "Debug", "Release" }
        architecture "x86_64"
        project "Gaem"
                language "c++"
                kind "ConsoleApp"
                --kind "WindowedApp"
                toolset "gcc" --"clang"
                cppdialect "C++20"
                rtti "Off"
                warnings "extra"
            --    files { "../src/*.cpp",
            --            "../src/*.hpp",
            --            "../src/games/*.cpp",
            --            "../src/games/*.hpp"
                      }
                targetdir "../"





                filter { "system:windows", "action:vs*" }
                        --systemversion(os.winSdkVersion() .. ".0") 
                        exceptionhandling "Off"     
                        buildoptions { "/fp:fast" }
                        defines { "_CRT_SECURE_NO_WARNINGS", 
                                  "_CRT_NONSTDC_NO_WARNINGS",
                                  protect_strings,
                                  remove_strings
                                }
                        files { "../src_win/init.cpp" }
                        location "../build_win"



                ignoreNaN = "-ffinite-math-only"
                addAddressSanitize = "-fsanitize=address" -- "undefined reference"
                utf8compiler = "-finput-charset=UTF-8 -fexec-charset=UTF-8 -fextended-identifiers"
                        
                filter { "system:ios" }
                        exceptionhandling "Off"
                        buildoptions = table.merge({ "-ffast-math"
                                                   },
                                                   nix_buildoptions
                                                  )
                        defines { protect_strings,
                                  remove_strings
                                }
                        files { "../src_ios/**" }
                        removefiles { "../src_ios/.DS_Store",
                                      "../src_ios/Assets.xcassets/.DS_Store"
                                    }
                        location "../build_ios"



                filter { "system:linux" }
                        buildoptions = table.merge({ "-march=native"
                                                   , "-pedantic"
                                                   , "-ffast-math"
                                                   , utf8compiler
                                                   },
                                                   nix_buildoptions
                                                  )
                        defines { protect_strings,
                                  remove_strings
                                }
                        files { "../src_linux/*.cpp",
                                "../src_linux/*.hpp",
                                "../src_server/*.cpp",
                                "../src_server/*.hpp"
                              }
                        location "../build_linux"
                        links { "pthread",
                                "crypto",
                                "ssl"
                              }
                        optimize "Speed" --"Size" --Debug" -- Speed" "Full"


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
