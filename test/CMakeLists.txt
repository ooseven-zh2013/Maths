# Maths 测试目标定义

# 注册一个测试可执行文件
# 参数 relative_source_path：相对本目录的源文件路径
# 目标名由「子目录名_文件名」组成，例如 integer/arithmetic.cpp -> integer_arithmetic
function(maths_add_test relative_source_path)
  get_filename_component(name ${relative_source_path} NAME_WE)
  get_filename_component(dir ${relative_source_path} DIRECTORY)
  get_filename_component(dirname ${dir} NAME)
  set(target_name ${dirname}_${name})

  add_executable(${target_name} ${relative_source_path})
  target_link_libraries(${target_name} PRIVATE Maths::Maths maths_warnings)
  target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
  add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

set(TEST_SOURCES
  integer/arithmetic.cpp
  integer/compare.cpp
  integer/edge_cases.cpp
  fraction/arithmetic.cpp
  fraction/compare_more.cpp
  fraction/io_tests.cpp
  algebraic/variable_tests.cpp
  algebraic/monomial_tests.cpp
  algebraic/polynomial_tests.cpp
  result/result_tests.cpp
  random/random_tests.cpp
  scope/scope_tests.cpp
  rational/rational_tests.cpp
  parser/expression_parser_tests.cpp
)

foreach(src IN LISTS TEST_SOURCES)
  maths_add_test(${src})
endforeach()
