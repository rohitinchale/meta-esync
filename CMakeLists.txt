cmake_minimum_required(VERSION 3.6)
project(tcu-updateagent)

if (USE_LEGACY_API)
    message("Building libua with legacy API")
    set(libName "libua.so")
else()
    message("Building libua with API version 2.0")
    set(LIBUA_API_VER "LIBUA_VER_2_0")
    add_definitions(-DLIBUA_VER_2_0)
    set(libName "libua_2.so")
endif()
set(CMAKE_VERBOSE_MAKEFILE on)
if (NOT BUILD_CONFIG)
    set(BUILD_CONFIG "config.cmk")
endif()

include(${BUILD_CONFIG})

execute_process(COMMAND git describe --dirty --exact-match --all --long WORKING_DIRECTORY ${UA_DIR} OUTPUT_VARIABLE UA_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
add_compile_definitions(UA_VERSION="${UA_VERSION}")

if (REMOTE_BROKER)
    add_definitions(-DREMOTE_BROKER)
endif()

if(SRC_GEN OR SRC_GEN_CONT OR SRC_GEN_COTA OR SRC_GEN_SM)
    include_directories(${SRC_GEN} ${SRC_GEN_CONT} ${SRC_GEN_COTA} ${SRC_GEN_SM})
else()
    include_directories(src-gen)
endif()

configure_file(${CMAKE_SOURCE_DIR}/json-c-rename.h ${CMAKE_BINARY_DIR} COPYONLY)

include_directories(${CMAKE_BINARY_DIR})

if(XL4BUS_DIR)
	include_directories(SYSTEM ${XL4BUS_DIR}/include)
	include_directories(SYSTEM ${XL4BUS_DIR}/../src/include)
	link_directories(${XL4BUS_DIR}/)
	message("XL4BUS_DIR include_directories directory path : ${XL4BUS_DIR}")
	message("XL4BUS_DIR link_directories directory path : ${XL4BUS_DIR}/")
	
endif()

if(UA_DIR)
	include_directories(SYSTEM ${UA_DIR}/../src/include)
	include_directories(SYSTEM ${UA_DIR}/../src)
	link_directories(${UA_DIR}/)
	message("UA_DIR directory path : ${UA_DIR}")
	set(UA_LIB ${UA_DIR}/${libName})
	message("UA_LIB path : ${UA_LIB}")
else()
	message("\nPlease, set the UA_DIR directory path in config.cmk file\n")
endif()

if (JSON_C_DIR)
    include_directories(SYSTEM ${JSON_C_DIR})
    link_directories(${JSON_C_DIR}/build)
    include_directories(${JSON_C_DIR}/build)
    set(JSON_C_LIBRARIES json-c)
    message(STATUS "${JSON_C_LIBRARIES}")
else()
    find_package(PkgConfig    REQUIRED)
    pkg_check_modules(JSON_C  REQUIRED json-c)
    message(STATUS "${JSON_C_LIBRARIES}")
    include_directories(SYSTEM ${JSON_C_INCLUDE_DIRS})
endif()

if (NOT DEFINED XL4CLIB_DIR)
    find_package(PkgConfig    REQUIRED)
    pkg_check_modules(XL4CLIB  REQUIRED xl4clib)
    include_directories(SYSTEM ${XL4CLIB_INCLUDE_DIRS})
else()
    set(XL4CLIB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/${XL4CLIB_DIR}/../include")
    set(XL4CLIB_LIBRARIES    "-Wl,-rpath,${CMAKE_CURRENT_SOURCE_DIR}/${XL4CLIB_DIR}: -lxl4clib")
    include_directories(SYSTEM ${XL4CLIB_INCLUDE_DIRS})
endif()

include_directories(${CMAKE_SOURCE_DIR}/json-c-build/)

include_directories("${WORKSPACE}/oss/prebuilt/vsomeip_3_3_0/usr/local/include")
include_directories("${WORKSPACE}/oss/prebuilt/boost_1_82_0/stage/include")
include_directories("${WORKSPACE}/oss/prebuilt/capicxx-someip-runtime_3_2_0/usr/local/include/CommonAPI-3.2/")
include_directories("${WORKSPACE}/oss/prebuilt/capicxx-core-runtime_3_2_0/usr/local/include/CommonAPI-3.2/")
include_directories("${WORKSPACE}/harman_5g/Harman/common/include/")
include_directories("${WORKSPACE}/harman_5g/Harman/harman_managers/fota_manager/common_api/generated/proxy")
include_directories("${WORKSPACE}/harman_5g/Harman/harman_managers/fota_manager/common_api/generated/common")
include_directories("${WORKSPACE}/excelfore/includes/")
include_directories("${HARMAN_DEP_PATH}/fota_manager")
include_directories("${HARMAN_DEP_PATH}/dm_manager/common_api/generated/proxy")
include_directories("${HARMAN_DEP_PATH}/dm_manager/common_api/generated/common")
add_custom_command(OUTPUT PHONY ${CMAKE_BINARY_DIR}/tcu_ua_version.h COMMAND ${CMAKE_SOURCE_DIR}/make_version.sh ARGS ${CMAKE_CURRENT_SOURCE_DIR} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
aux_source_directory(${CMAKE_SOURCE_DIR}/src SRC_FILES)

if (DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -s")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wimplicit -Wreturn-type -Werror")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

set(BUILD_BINS TRUE)

set(JSON_C_LIB ${CMAKE_SOURCE_DIR}/json-c-build/.libs/libjson-c.a) 
if (NOT EXISTS ${JSON_C_LIB})
set(JSON_C_LIB ${CMAKE_SOURCE_DIR}/json-c-build/.libs/libjson-c.so)
endif()

pkg_check_modules(JSON_C  REQUIRED json-c)
include_directories(JSON_C_INCLUDE_DIRS)

set(DEPEND_LIBS ${DEPEND_LIBS}
-lboost_system
-lboost_thread
-lboost_filesystem
-lboost_chrono
-lboost_date_time
-lboost_atomic
-lboost_serialization
-lvsomeip3
-llog
-lCommonAPI
-lCommonAPI-SomeIP
-lseccryptoclntlib
  )
link_directories("${WORKSPACE}/oss/prebuilt/boost_1_82_0/stage/lib")
link_directories("${WORKSPACE}/oss/prebuilt/vsomeip_3_3_0/usr/local/lib")
link_directories("${WORKSPACE}/oss/prebuilt/capicxx-someip-runtime_3_2_0/usr/local/lib")
link_directories("${WORKSPACE}/oss/prebuilt/capicxx-core-runtime_3_2_0/usr/local/lib")
link_directories("${WORKSPACE}/harman_5g/Harman/harman_security/seccryptomgr/lib")
link_directories("${WORKSPACE}/excelfore/libs/")

add_executable(tcu-updateagent ${SRC_FILES} ${CMAKE_BINARY_DIR}/tcu_ua_version.h
${SRC_GEN}/v1/commonapi/toscore/sdmservice/SelfDiagnosticManagerSomeIPProxy.cpp
${SRC_GEN}/v1/commonapi/toscore/sdmservice/SelfDiagnosticManagerSomeIPDeployment.cpp
${SRC_GEN_COTA}/v1/commonapi/toscore/cmsservice/ConfigurationManagerSomeIPProxy.cpp
${SRC_GEN_COTA}/v1/commonapi/toscore/cmsservice/ConfigurationManagerSomeIPDeployment.cpp
${SRC_GEN_CONT}/v1/commonapi/toscore/cmservice/ContainerManagerSomeIPProxy.cpp
${SRC_GEN_CONT}/v1/commonapi/toscore/cmservice/ContainerManagerSomeIPDeployment.cpp
${SRC_GEN_SM}/v0/commonapi/toscore/smservice/SystemManagerSomeIPProxy.cpp
${SRC_GEN_SM}/v0/commonapi/toscore/smservice/SystemManagerSomeIPDeployment.cpp)

target_link_libraries(tcu-updateagent -lm ua_2 xl4bus zip xml2 ${XL4CLIB_LIBRARIES} ${DEPEND_LIBS} zstd json-c crypto udsutils)

install(TARGETS ${CMAKE_PROJECT_NAME} RUNTIME DESTINATION bin)
