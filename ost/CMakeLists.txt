build_lib(
    LIBNAME ost
    SOURCE_FILES helper/ost-helper.cc
                 model/ost_node.cc
                 model/ost-header.cc
                 model/timer_fifo.cc
    HEADER_FILES helper/ost-helper.h
                 model/ost_node.h
                 model/ost-header.h
                 model/timer_fifo.h
    LIBRARIES_TO_LINK 
        ${libnetwork}
    TEST_SOURCES test/ost-test-suite.cc
                 ${examples_as_tests_sources}
)
