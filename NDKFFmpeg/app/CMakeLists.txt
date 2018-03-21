cmake_minimum_required(VERSION 3.4.1)


include_directories(./libs/include
                    ./libs/include/libyuv)

link_directories(./libs/${ANDROID_ABI})

find_library(log-lib
             log)
find_library(android-lib
             android)

add_library(
             native-lib
             SHARED
             src/main/cpp/native-lib.cpp )
add_library(
             native-player
             SHARED
             src/main/cpp/native-player.cpp
             src/main/cpp/queue.cpp)

target_link_libraries(native-lib
                       ${log-lib}
                       ${android-lib}
                       avutil-54
                       swresample-1
                       avcodec-56
                       avformat-56
                       swscale-3
                       postproc-53
                       avfilter-5
                       avdevice-56
                       yuv)
target_link_libraries(native-player
                       ${log-lib}
                       ${android-lib}
                       avutil-54
                       swresample-1
                       avcodec-56
                       avformat-56
                       swscale-3
                       postproc-53
                       avfilter-5
                       avdevice-56
                       yuv)