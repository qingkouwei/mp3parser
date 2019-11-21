# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html

# Sets the minimum version of CMake required to build the native library.

cmake_minimum_required(VERSION 3.4.1)
project (mp3parser)
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")
add_library(libiconv SHARED IMPORTED)
set_target_properties(libiconv PROPERTIES IMPORTED_LOCATION
    /usr/lib/libiconv.dylib)


aux_source_directory(${CMAKE_HOME_DIRECTORY} SRC_LIST)
add_executable(
        mp3parser
        ${SRC_LIST}
        )
target_link_libraries(mp3parser
        iconv)
