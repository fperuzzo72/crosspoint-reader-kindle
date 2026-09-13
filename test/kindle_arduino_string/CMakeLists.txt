add_executable(KindleArduinoStringTest
  KindleArduinoStringTest.cpp
  ${REPO_ROOT}/lib/hal/kindle/ArduinoString.cpp
)

target_include_directories(KindleArduinoStringTest PRIVATE
  ${REPO_ROOT}/lib/hal/kindle
)

target_link_libraries(KindleArduinoStringTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KindleArduinoStringTest)
