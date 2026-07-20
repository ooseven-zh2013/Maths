# CMake test targets for Maths

cmake_minimum_required(VERSION 3.15)

include(GNUInstallDirs)

set(TEST_SOURCES_INT
  integer/arithmetic.cpp
  integer/compare.cpp
  integer/edge_cases.cpp
)

set(TEST_SOURCES_FRAC
  fraction/arithmetic.cpp
  fraction/compare_more.cpp
  fraction/io_tests.cpp
)

set(TEST_SOURCES_ALG
  algebraic/variable_tests.cpp
)

foreach(src IN LISTS TEST_SOURCES_INT)
  get_filename_component(name ${src} NAME_WE)
  get_filename_component(dir ${src} DIRECTORY)
  get_filename_component(dirname ${dir} NAME)
  set(target_name ${dirname}_${name})
  add_executable(${target_name} ${src})
  target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
  add_test(NAME ${target_name} COMMAND ${CMAKE_COMMAND} -E env "LC_ALL=C.UTF-8" $<TARGET_FILE:${target_name}>)
endforeach()

foreach(src IN LISTS TEST_SOURCES_FRAC)
  get_filename_component(name ${src} NAME_WE)
  get_filename_component(dir ${src} DIRECTORY)
  get_filename_component(dirname ${dir} NAME)
  set(target_name ${dirname}_${name})
  add_executable(${target_name} ${src})
  target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
  # some fraction tests expect stdin; wrap with /bin/sh -c to provide input where needed
  if(name STREQUAL "arithmetic")
    add_test(NAME ${target_name} COMMAND /bin/sh -c "printf '3/4\n' | $<TARGET_FILE:${target_name}>")
  else()
    add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
  endif()
endforeach()

foreach(src IN LISTS TEST_SOURCES_ALG)
  get_filename_component(name ${src} NAME_WE)
  get_filename_component(dir ${src} DIRECTORY)
  get_filename_component(dirname ${dir} NAME)
  set(target_name ${dirname}_${name})
  add_executable(${target_name} ${src})
  target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
  add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
endforeach()
