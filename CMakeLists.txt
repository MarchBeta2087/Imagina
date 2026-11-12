cmake_minimum_required (VERSION 3.16)

project ("Imagina" LANGUAGES C CXX)

option(ENABLE_CUSTOM_FORMULA "Enable custom formula" OFF)
option(MPIR_USE_DLL "Use dll version of MPIR" ON)
option(BUILD_CODEC_TEST "Build the raw map codec round trip test" OFF)

# Front-end: the Win32 one (window, menus and dialogs from MainWindow.cpp) or
# the portable SDL3 + Dear ImGui one.  The Win32 front-end only builds on
# Windows; the SDL one builds everywhere SDL3 and OpenGL are available.
if(WIN32)
	set(IMAGINA_FRONTEND_DEFAULT "win32")
else()
	set(IMAGINA_FRONTEND_DEFAULT "sdl")
endif()
set(IMAGINA_FRONTEND "${IMAGINA_FRONTEND_DEFAULT}" CACHE STRING "Front-end: win32 or sdl")
set_property(CACHE IMAGINA_FRONTEND PROPERTY STRINGS win32 sdl)

if(IMAGINA_FRONTEND STREQUAL "win32")
	if(WIN32)
		set(BUILD_GUI_DEFAULT ON)
	else()
		set(BUILD_GUI_DEFAULT OFF)
		message(WARNING "IMAGINA_FRONTEND=win32 needs Windows; use -DIMAGINA_FRONTEND=sdl.")
	endif()
else()
	set(BUILD_GUI_DEFAULT ON)
endif()
option(BUILD_GUI "Build the Imagina application" ${BUILD_GUI_DEFAULT})

set(INCLUDE_PATH "include" CACHE PATH "Include path")
set(LIBRARY_PATH "lib" CACHE PATH "Library path")

# Library names differ between toolchains (zlibwapi.lib for the Visual Studio
# build, libz for most GNU/Clang setups), so they can be overridden on the
# command line: -DZLIB_LIBRARY=z -DMPIR_LIBRARY=mpir ...
if(WIN32)
	set(ZLIB_LIBRARY_DEFAULT "zlibwapi")
else()
	set(ZLIB_LIBRARY_DEFAULT "z")
endif()
set(ZLIB_LIBRARY "${ZLIB_LIBRARY_DEFAULT}" CACHE STRING "Name of the zlib library")
set(PNG_LIBRARY "libpng16" CACHE STRING "Name of the libpng library")
set(MPIR_LIBRARY "mpir" CACHE STRING "Name of the MPIR library")
set(MPIRXX_LIBRARY "mpirxx" CACHE STRING "Name of the MPIR C++ library")

# The application is written for AVX2, which only exists on x86.
set(Imagina_ArchFlags "")
if(MSVC)
	set(Imagina_ArchFlags "/arch:AVX2")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|X86_64|AMD64|amd64|i[3-6]86)$")
	set(Imagina_ArchFlags "-mavx2" "-mfma")
endif()

# GNU/Clang releases are built with -O2, not with CMake's default -O3: the -O3
# build of the computation crashes intermittently (inside
# FractalContext::Update() or later in the OpenGL driver), while the same
# sources are stable at -O2 and with MSVC's /O2.  This matches the Windows
# release flags the project has always used.
if(NOT MSVC)
	set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
	set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
	set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG" CACHE STRING "" FORCE)
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG" CACHE STRING "" FORCE)
endif()

# Link time optimisation is opt-in and untested with the GCC/Clang builds.
option(IMAGINA_ENABLE_LTO "Build with link time optimisation" OFF)
include(CheckIPOSupported)
if(IMAGINA_ENABLE_LTO)
	check_ipo_supported(RESULT Imagina_IpoSupported OUTPUT Imagina_IpoError LANGUAGES CXX)
	if(NOT Imagina_IpoSupported)
		message(STATUS "Link time optimisation is not available, building without it.")
		set(IMAGINA_ENABLE_LTO OFF)
	endif()
endif()

# Applies the compiler settings shared by every target.  Called from the
# directory scope so that the properties also cover the source file specific
# flags below.
function(ImaginaApplyCompilerOptions Target)
	set_property(TARGET ${Target} PROPERTY CXX_STANDARD 20)
	set_property(TARGET ${Target} PROPERTY CXX_STANDARD_REQUIRED ON)
	if(MSVC)
		target_compile_options(${Target} PRIVATE /W3 /GR /EHsc /bigobj ${Imagina_ArchFlags})
	else()
		# GCC and Clang: the same set of options the Visual Studio build uses,
		# expressed in the GNU dialect (clang-cl is handled by the MSVC branch).
		target_compile_options(${Target} PRIVATE ${Imagina_ArchFlags} -fno-math-errno)
	endif()

	if(IMAGINA_ENABLE_LTO)
		set_property(TARGET ${Target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	endif()
endfunction()

# HInfLAEvaluator.cpp is compiled with relaxed floating point rules; the exact
# spelling depends on the compiler.
function(ImaginaApplyFastMathFlags)
	if(MSVC)
		set(Flags "/fp:fast")
	else()
		set(Flags "-fassociative-math" "-ffp-contract=fast")
	endif()
	set_source_files_properties(src/HInfLAEvaluator.cpp PROPERTIES COMPILE_OPTIONS "${Flags}" SKIP_PRECOMPILE_HEADERS ON)
endfunction()

# Sources shared by both front-ends: the fractal computation and the OpenGL
# drawing code.  Everything here is platform independent.
set(ImaginaCoreSources
	src/Computation.cpp
	src/Evaluator.cpp
	src/FeatureFinder.cpp
	src/File.cpp
	src/FractalContext.cpp
	src/GLUtils.cpp
	src/Global.cpp
	src/HInfLAEvaluator.cpp
	src/JitEvaluator.cpp
	src/PixelManager.cpp
	src/RawMapCodec.cpp
	src/Render.cpp
	src/platform/PlatformMemory.cpp
)

if(ENABLE_CUSTOM_FORMULA)
	FILE(GLOB SourceFilesEncoder "src/encoder/*.cpp")
else()
	set(SourceFilesEncoder "")
endif()

if(BUILD_GUI AND IMAGINA_FRONTEND STREQUAL "win32")
	if(NOT WIN32)
		message(FATAL_ERROR "The win32 front-end needs Windows; configure with -DIMAGINA_FRONTEND=sdl.")
	endif()

	add_executable (Imagina WIN32
		${ImaginaCoreSources}
		${SourceFilesEncoder}
		src/win32/main.cpp
		src/win32/MainWindow.cpp
		src/win32/ZoomExport.cpp
		src/win32/PlatformDependent.cpp
		src/win32/Resource.rc)

	ImaginaApplyCompilerOptions(Imagina)
	ImaginaApplyFastMathFlags()

	target_include_directories(Imagina PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/win32"
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_precompile_headers (Imagina PRIVATE "src/Includes.h")
	set_source_files_properties("src/win32/Resource.rc" PROPERTIES LANGUAGE RC)
	target_compile_definitions(Imagina PRIVATE UNICODE)

	target_include_directories(Imagina SYSTEM PRIVATE ${INCLUDE_PATH})
	target_link_directories(Imagina PRIVATE ${LIBRARY_PATH})
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		target_include_directories(Imagina SYSTEM PRIVATE "${INCLUDE_PATH}/Debug")
		target_link_directories(Imagina PRIVATE "${LIBRARY_PATH}/Debug")
	else()
		target_include_directories(Imagina SYSTEM PRIVATE "${INCLUDE_PATH}/Release")
		target_link_directories(Imagina PRIVATE "${LIBRARY_PATH}/Release")
	endif()

	target_link_libraries(Imagina "opengl32")
	target_link_libraries(Imagina "comctl32")
	target_link_libraries(Imagina ${PNG_LIBRARY})
	target_link_libraries(Imagina ${ZLIB_LIBRARY})
	target_link_libraries(Imagina "dwmapi")
	target_link_libraries(Imagina ${MPIR_LIBRARY})

	if(MPIR_USE_DLL)
		target_compile_definitions(Imagina PRIVATE MSC_USE_DLL)
	else()
		target_link_libraries(Imagina ${MPIRXX_LIBRARY})
	endif()

	if(ENABLE_CUSTOM_FORMULA)
		target_link_libraries(Imagina "mpfr")
		target_link_libraries(Imagina "mpc")
		target_link_libraries(Imagina "symengine")

		target_compile_definitions(Imagina PRIVATE ENABLE_CUSTOM_FORMULA)
	endif()

	if(MSVC)
		# The .rc file already carries a manifest, and the static MPIR build
		# produces a duplicate PDB reference warning.
		target_link_options(Imagina PRIVATE /MANIFEST:NO /IGNORE:4099)
	else()
		# wWinMain needs the unicode entry point under MinGW-w64, and the
		# linker's own default manifest cannot be combined with the one in
		# Resource.rc.
		target_link_options(Imagina PRIVATE -municode)
		set_source_files_properties("src/win32/Resource.rc" PROPERTIES COMPILE_DEFINITIONS "IMAGINA_NO_EMBEDDED_MANIFEST")
	endif()
endif()

if(BUILD_GUI AND IMAGINA_FRONTEND STREQUAL "sdl")
	# SDL3: use an installed copy when there is one, otherwise build it from
	# source (needs network access the first time).
	find_package(SDL3 3.2 CONFIG QUIET)
	if(NOT SDL3_FOUND)
		message(STATUS "SDL3 not found, fetching it from GitHub")
		include(FetchContent)
		set(SDL_SHARED OFF CACHE BOOL "" FORCE)
		set(SDL_STATIC ON CACHE BOOL "" FORCE)
		set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
		set(SDL_TESTS OFF CACHE BOOL "" FORCE)
		set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
		set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
		FetchContent_Declare(SDL3
			GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
			GIT_TAG release-3.4.20
			GIT_SHALLOW TRUE)
		FetchContent_MakeAvailable(SDL3)
	endif()

	# Dear ImGui: a checkout can be pointed at with -DIMAGINA_IMGUI_PATH=...,
	# otherwise it is fetched as well.
	set(IMAGINA_IMGUI_PATH "" CACHE PATH "Path to a Dear ImGui checkout")
	if(IMAGINA_IMGUI_PATH)
		set(ImGuiDir "${IMAGINA_IMGUI_PATH}")
	else()
		message(STATUS "Dear ImGui not found, fetching it from GitHub")
		include(FetchContent)
		FetchContent_Declare(ImGui
			GIT_REPOSITORY https://github.com/ocornut/imgui.git
			GIT_TAG v1.92.9
			GIT_SHALLOW TRUE)
		FetchContent_Populate(ImGui)
		set(ImGuiDir "${imgui_SOURCE_DIR}")
	endif()

	add_library(ImaginaImGui STATIC
		"${ImGuiDir}/imgui.cpp"
		"${ImGuiDir}/imgui_draw.cpp"
		"${ImGuiDir}/imgui_tables.cpp"
		"${ImGuiDir}/imgui_widgets.cpp"
		"${ImGuiDir}/misc/cpp/imgui_stdlib.cpp"
		"${ImGuiDir}/backends/imgui_impl_sdl3.cpp"
		"${ImGuiDir}/backends/imgui_impl_opengl3.cpp")
	target_include_directories(ImaginaImGui PUBLIC "${ImGuiDir}" "${ImGuiDir}/backends" "${ImGuiDir}/misc/cpp")
	set_property(TARGET ImaginaImGui PROPERTY CXX_STANDARD 17)
	target_link_libraries(ImaginaImGui PUBLIC SDL3::SDL3)

	add_executable (Imagina
		${ImaginaCoreSources}
		${SourceFilesEncoder}
		src/sdl/SDLMain.cpp)

	ImaginaApplyCompilerOptions(Imagina)

	# The front-end lives in src/sdl and includes the shared headers by name.
	target_include_directories(Imagina PRIVATE
		"${CMAKE_CURRENT_SOURCE_DIR}/src"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/platform"
		"${CMAKE_CURRENT_SOURCE_DIR}/src/sdl"
		"${CMAKE_CURRENT_SOURCE_DIR}")
	target_include_directories(Imagina SYSTEM PRIVATE ${INCLUDE_PATH})
	target_link_directories(Imagina PRIVATE ${LIBRARY_PATH})
	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
		target_include_directories(Imagina SYSTEM PRIVATE "${INCLUDE_PATH}/Debug")
		target_link_directories(Imagina PRIVATE "${LIBRARY_PATH}/Debug")
	else()
		target_include_directories(Imagina SYSTEM PRIVATE "${INCLUDE_PATH}/Release")
		target_link_directories(Imagina PRIVATE "${LIBRARY_PATH}/Release")
	endif()

	target_link_libraries(Imagina PRIVATE ImaginaImGui SDL3::SDL3 ${MPIR_LIBRARY} ${PNG_LIBRARY} ${ZLIB_LIBRARY})
	if(NOT MPIR_USE_DLL)
		target_link_libraries(Imagina PRIVATE ${MPIRXX_LIBRARY})
	elseif(WIN32)
		target_compile_definitions(Imagina PRIVATE MSC_USE_DLL)
	endif()

	if(WIN32)
		target_link_libraries(Imagina PRIVATE opengl32 dbghelp)
	else()
		find_package(OpenGL REQUIRED COMPONENTS OpenGL)
		target_link_libraries(Imagina PRIVATE OpenGL::GL)
		target_compile_definitions(Imagina PRIVATE IMAGINA_SDL)
	endif()

	if(ENABLE_CUSTOM_FORMULA)
		target_link_libraries(Imagina PRIVATE "mpfr" "mpc" "symengine")
		target_compile_definitions(Imagina PRIVATE ENABLE_CUSTOM_FORMULA)
	endif()

	# The licence is shown by the About dialog; the Win32 build embeds it as a
	# resource instead.
	target_compile_definitions(Imagina PRIVATE
		"IMAGINA_LICENSE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt\"")

	if(APPLE)
		set_target_properties(Imagina PROPERTIES MACOSX_BUNDLE TRUE)
	endif()
endif()

if(BUILD_CODEC_TEST)
	add_executable (RawMapCodecTest "tests/RawMapCodecTest.cpp" "src/RawMapCodec.cpp")
	ImaginaApplyCompilerOptions(RawMapCodecTest)
	target_include_directories(RawMapCodecTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
	target_include_directories(RawMapCodecTest SYSTEM PRIVATE ${INCLUDE_PATH})
	target_link_directories(RawMapCodecTest PRIVATE ${LIBRARY_PATH})
	target_link_libraries(RawMapCodecTest ${ZLIB_LIBRARY})
endif()

if(NOT BUILD_GUI AND NOT BUILD_CODEC_TEST)
	message(WARNING "Nothing will be built: enable BUILD_GUI and/or BUILD_CODEC_TEST.")
endif()
