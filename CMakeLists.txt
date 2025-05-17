cmake_minimum_required(VERSION 3.14)

set(QMLAV_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

set(QMLAV_FILES
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavwaitingqueue.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavpropertyhelpers.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavmediacontextholder.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavutils.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavutils.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavoptions.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavoptions.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavthread.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavthread.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavdemuxer.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavdemuxer.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavdecoder.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavdecoder.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavformat.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavformat.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavframe.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavframe.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavresampler.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavresampler.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavvideobuffer.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavvideobuffer.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavhwoutput.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavhwoutput.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavaudioiodevice.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavaudioiodevice.h
    ${CMAKE_CURRENT_LIST_DIR}/src/qmlavplayer.cpp ${CMAKE_CURRENT_LIST_DIR}/src/qmlavplayer.h
)

set(QMLAV_LINK_DEPENDENCIES avformat avcodec avutil swscale swresample avdevice)
if (UNIX AND NOT ANDROID)
    list(APPEND QMLAV_LINK_DEPENDENCIES va va-x11 GL GLX X11)
endif()

set(QMLAV_INCLUDE ${QMLAV_INCLUDE} PARENT_SCOPE)
set(QMLAV_FILES ${QMLAV_FILES} PARENT_SCOPE)
set(QMLAV_LINK_DEPENDENCIES ${QMLAV_LINK_DEPENDENCIES} PARENT_SCOPE)

# Tests
if (NOT ANDROID)
    add_subdirectory(src/tests)
endif()
