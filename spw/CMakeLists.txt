build_lib(
  LIBNAME spw
  SOURCE_FILES
    model/spw-channel.cc
    model/spw-device.cc
  HEADER_FILES
    model/spw-channel.h
    model/spw-device.h
  LIBRARIES_TO_LINK
    ${libnetwork}
  TEST_SOURCES test/spw-test-suite.cc
)
