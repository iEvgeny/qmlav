cmake_minimum_required(VERSION 3.14)

set(QMLAV_FILES
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavaudioqueue.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavaudioqueue.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavdecoder.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavdecoder.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavdemuxer.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavdemuxer.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavformat.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavformat.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavframe.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavframe.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavplayer.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavplayer.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavutils.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavutils.h
    PARENT_SCOPE
)
