build_lib(
  LIBNAME ost
  SOURCE_FILES
    model/ost_node.cc
    model/ost_socket.cc
    model/ost-header.cc
    model/timer_fifo.cc
  HEADER_FILES
	model/ost_node.h
  LIBRARIES_TO_LINK ${core} 
  TEST_SOURCES 
	test/ost-simple-test.cc
	test/ost-compare-test.cc
)
