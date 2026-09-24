# JPEGDEC is fetched into build/kindle/thirdparty by tools/kindle/fetch-deps.sh
# rather than committed, so test/CMakeLists.txt registers this directory only
# when the source is actually there. See the header of JpegProgressiveTest.cpp.
add_executable(JpegProgressiveTest
  JpegProgressiveTest.cpp
  ${JPEGDEC_SRC}
)

target_include_directories(JpegProgressiveTest PRIVATE
  ${JPEGDEC_DIR}
)

target_link_libraries(JpegProgressiveTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

# JPEGDEC is upstream code plus our two patches; its warnings are not ours to
# fix, and crosspoint_test_common turns on -Wall -Wextra -pedantic.
set_source_files_properties(${JPEGDEC_SRC} PROPERTIES COMPILE_OPTIONS "-w")

gtest_discover_tests(JpegProgressiveTest)
