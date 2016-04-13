cmake_minimum_required(VERSION 3.2)

#include_directories("/usr/local/include")

find_library(curl_LIBRARY NAMES curl PATHS "/usr/local/lib")
find_package(OpenSSL REQUIRED)

include(../../../w/common/macros.cmake)
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(../../../w/)
include_directories(${CMAKE_BINARY_DIR})

gen_version_c()
add_subdirectory(../../../w/common "${CMAKE_BINARY_DIR}/common")

set(
	RESOURCES
	synopsis.txt
	usage.txt
)

generate_resources("${RESOURCES}" GENERATED)

add_executable(
	mailbot
	mailbot.c
	conf.c conf.h
	version.c version.h
	util.c util.h
	imap.c imap.h
	${GENERATED}
)

target_link_libraries(
	mailbot
	${curl_LIBRARY}
	svc
)

install(TARGETS mailbot RUNTIME DESTINATION bin)
