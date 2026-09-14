add_executable(KindleTouchTest
  KindleTouchTest.cpp
  ${REPO_ROOT}/lib/hal/kindle/KindleTouchClassifier.cpp
)

target_include_directories(KindleTouchTest PRIVATE
  ${REPO_ROOT}/lib/hal/kindle
)

target_link_libraries(KindleTouchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KindleTouchTest)
