cmake_minimum_required(VERSION 3.21)

project(QMLScrubPlayerConsumerSmoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(QMLScrubPlayer CONFIG REQUIRED)

add_executable(qmlscrubplayer_consumer_smoke main.cpp)
target_link_libraries(qmlscrubplayer_consumer_smoke PRIVATE QMLScrubPlayer::QMLScrubPlayer)
