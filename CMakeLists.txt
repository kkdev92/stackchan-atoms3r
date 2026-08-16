# The ESP-IDF project definition. It sits at the root because that is where
# ESP-IDF looks for it.
#
# Where components live:
#   src/core      the decisions; buildable on a host. ESP-IDF must not appear
#   src/platform  the device only. One responsibility per component
#
# Directories listed here are searched one level deep. Add any deeper component
# root explicitly.
#
# COMPONENTS narrows the build to main and what it needs. PlatformIO does the
# same for main, src and __pio_env.
#
# Components that contribute required Kconfig options must be named here even
# when project code does not reference them directly. Verify the generated
# sdkconfig after changing components or configuration fragments.
cmake_minimum_required(VERSION 3.16.0)

list(APPEND EXTRA_COMPONENT_DIRS
  ${CMAKE_CURRENT_LIST_DIR}/src/core
  ${CMAKE_CURRENT_LIST_DIR}/src/platform)

set(COMPONENTS main src __pio_env espcoredump)

# The version device.describe reports.
#
# Set explicitly so device.describe reports the release version consistently,
# including when the project is built from a source archive without Git
# metadata.
#
# Keep this in step with the release tag.
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stackchan_atoms3r)
