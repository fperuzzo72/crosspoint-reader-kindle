add_executable(MultipartParserTest
  MultipartParserTest.cpp
  ${REPO_ROOT}/lib/hal/kindle/MultipartParser.cpp
)

target_include_directories(MultipartParserTest PRIVATE
  ${REPO_ROOT}/lib/hal/kindle
)

target_link_libraries(MultipartParserTest PRIVATE
  crosspoint_test_common
)

add_test(NAME MultipartParser COMMAND MultipartParserTest)
