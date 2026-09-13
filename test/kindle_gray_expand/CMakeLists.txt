add_executable(KindleGrayExpandTest
  KindleGrayExpandTest.cpp
  ${REPO_ROOT}/lib/hal/kindle/KindleGrayExpand.cpp
)

target_include_directories(KindleGrayExpandTest PRIVATE
  ${REPO_ROOT}/lib/hal/kindle
)

target_link_libraries(KindleGrayExpandTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KindleGrayExpandTest)
