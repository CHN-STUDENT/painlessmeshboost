cmake_minimum_required (VERSION 2.6)
project (painlessMeshBoost)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

SET(Boost_USE_STATIC_LIBS OFF)
SET(Boost_USE_MULTITHREAD ON)

FIND_PACKAGE(Boost 1.42.0 REQUIRED COMPONENTS filesystem system thread regex program_options)
IF(Boost_FOUND)
  INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
  LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
ENDIF(Boost_FOUND)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")


ADD_EXECUTABLE(painlessMeshBoost src/main.cpp src/fake_serial.cpp libs/painlessMesh/src/painlessMeshConnection.cpp libs/painlessMesh/src/scheduler.cpp)
target_include_directories(painlessMeshBoost PUBLIC src/ libs/TaskScheduler/src/ libs/ArduinoJson/ libs/painlessMesh/src/ libs/uuid/include/)
TARGET_LINK_LIBRARIES(painlessMeshBoost ${Boost_LIBRARIES})
TARGET_LINK_LIBRARIES(painlessMeshBoost -lfcgi++)
TARGET_LINK_LIBRARIES(painlessMeshBoost -lfcgi)