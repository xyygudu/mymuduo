cmake_minimum_required (VERSION 2.8)

project (TestAsyncLog)

include_directories(
    ../../base
    ../
    )
aux_source_directory(. DIR_)
aux_source_directory(../ DIR_SRCS)
aux_source_directory(../../base DIR_BASE)


add_executable(TestAsyncLog  ${DIR_BASE} ${DIR_SRCS} Logtest.cc) 
target_link_libraries(TestAsyncLog pthread)