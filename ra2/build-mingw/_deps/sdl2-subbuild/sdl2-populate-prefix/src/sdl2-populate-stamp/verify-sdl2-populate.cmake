# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

if("/home/mmurphy/projects/rallypoint/ra2/third-party/SDL2-2.30.9.tar.gz" STREQUAL "")
  message(FATAL_ERROR "LOCAL can't be empty")
endif()

if(NOT EXISTS "/home/mmurphy/projects/rallypoint/ra2/third-party/SDL2-2.30.9.tar.gz")
  message(FATAL_ERROR "File not found: /home/mmurphy/projects/rallypoint/ra2/third-party/SDL2-2.30.9.tar.gz")
endif()

if("SHA256" STREQUAL "")
  message(WARNING "File cannot be verified since no URL_HASH specified")
  return()
endif()

if("24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4" STREQUAL "")
  message(FATAL_ERROR "EXPECT_VALUE can't be empty")
endif()

message(VERBOSE "verifying file...
     file='/home/mmurphy/projects/rallypoint/ra2/third-party/SDL2-2.30.9.tar.gz'")

file("SHA256" "/home/mmurphy/projects/rallypoint/ra2/third-party/SDL2-2.30.9.tar.gz" actual_value)

if(NOT "${actual_value}" STREQUAL "24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4")
  message(FATAL_ERROR "error: SHA256 hash of
  /home/mmurphy/projects/rallypoint/ra2/third-party/SDL2-2.30.9.tar.gz
does not match expected value
  expected: '24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4'
    actual: '${actual_value}'
")
endif()

message(VERBOSE "verifying file... done")
