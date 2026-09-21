# 示例程序

add_executable(maths_simplify simplify/main.cpp)
target_link_libraries(maths_simplify PRIVATE Maths::Maths maths_warnings)

# 只安装可执行文件，并归入独立的 apps 组件。
# scripts/build-releases.sh 用 --component apps 过滤，因此 releases/ 里
# 只会有可执行文件，不会混入头文件或 CMake 包配置。
install(TARGETS maths_simplify RUNTIME DESTINATION . COMPONENT apps)

# 聚合目标：新增程序时记得加进 DEPENDS。
# scripts/build-releases.sh 只构建它，从而跳过测试等无关目标。
add_custom_target(maths_apps DEPENDS maths_simplify)
