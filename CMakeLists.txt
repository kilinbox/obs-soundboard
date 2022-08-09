cmake_minimum_required(VERSION 3.16...3.21)

# Change obs-plugintemplate to your plugin's name in a machine-readable format
# (e.g.: obs-myawesomeplugin) and set
project(obs-soundboard VERSION 1.1.0)

# Qt build stuff
set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON) 

# Replace `Your Name Here` with the name (yours or your organization's) you want
# to see as the author of the plugin (in the plugin's metadata itself and in the
# installers)
set(PLUGIN_AUTHOR "cg2121")

# Replace `com.example.obs-plugin-template` with a unique Bundle ID for macOS
# releases (used both in the installer and when submitting the installer for
# notarization)
set(MACOS_BUNDLEID "com.example.${CMAKE_PROJECT_NAME}")

# Replace `me@contoso.com` with the maintainer email address you want to put in
# Linux packages
set(LINUX_MAINTAINER_EMAIL "claytong1214@gmail.com")

# Setup target
add_library(obs-soundboard MODULE)
add_library(OBS::obs-soundboard ALIAS obs-soundboard)

# Add your custom source files here - header files are optional and only
# required for visibility e.g. in Xcode or Visual Studio
target_sources(
  obs-soundboard
  PRIVATE src/Soundboard.ui
          src/SoundEdit.ui
          src/media-controls.ui
          src/sound-data.cpp
          src/sound-data.hpp
          src/scene-tree.cpp
          src/soundboard.cpp
          src/volume-control.cpp
          src/slider-absoluteset-style.cpp
          src/slider-ignorewheel.cpp
          src/adv-audio-control.cpp
          src/media-controls.cpp
          src/media-slider.cpp
          src/plugin-macros.generated.h
          src/scene-tree.hpp
          src/soundboard.hpp
          src/volume-control.hpp
          src/slider-absoluteset-style.hpp
          src/slider-ignorewheel.hpp
          src/mute-checkbox.hpp
          src/adv-audio-control.hpp
          src/balance-slider.hpp
          src/clickable-label.hpp
          src/media-controls.hpp
          src/media-slider.hpp)

# /!\ TAKE NOTE: No need to edit things past this point /!\

find_qt(COMPONENTS Widgets Core)

configure_file(src/plugin-macros.h.in
               src/plugin-macros.generated.h)

target_sources(obs-soundboard PRIVATE src/plugin-macros.generated.h)

# --- Platform-independent build settings ---
target_include_directories(
  obs-soundboard
  PRIVATE ${Qt5Core_INCLUDES} ${Qt5Widgets_INCLUDES} "src")

target_link_libraries(
  obs-soundboard
  PRIVATE OBS::libobs
          OBS::frontend-api
          Qt::Core
          Qt::Widgets)

target_compile_features(obs-soundboard PRIVATE cxx_std_17)

# --- End of section ---

# --- Windows-specific build settings and tasks ---
if(WIN32)
  configure_file(cmake/bundle/windows/installer-Windows.iss.in
                 ${CMAKE_BINARY_DIR}/installer-Windows.generated.iss)

  if(MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /MP /d2FH4-)
  endif()
  # --- End of section ---

  # -- macOS specific build settings and tasks --
elseif(APPLE)
  configure_file(cmake/bundle/macos/installer-macos.pkgproj.in
                 ${CMAKE_BINARY_DIR}/installer-macos.generated.pkgproj)

  set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
  set(MACOSX_PLUGIN_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}")
  set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")

  target_compile_options(
    obs-soundboard
    PRIVATE -Wall -Wextra -Werror-implicit-function-declaration -stdlib=libc++
            -fvisibility=default)

  set_target_properties(obs-soundboard PROPERTIES PREFIX "")
  # --- End of section ---

  # --- Linux-specific build settings and tasks ---
else()
  target_compile_options(obs-soundboard PRIVATE -Wall -Wextra)

  set_target_properties(obs-soundboard PROPERTIES PREFIX "")
endif()
# --- End of section ---

setup_plugin_target(obs-soundboard)
