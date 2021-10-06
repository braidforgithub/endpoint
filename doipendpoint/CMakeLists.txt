project(endpoint)

find_package( Boost 1.55 COMPONENTS system thread filesystem REQUIRED )
include_directories( ${Boost_INCLUDE_DIR} )

if(Boost_FOUND)
  if(Boost_LIBRARY_DIR)
    MESSAGE( STATUS "Boost_LIBRARY_DIR not empty using it: ${Boost_LIBRARY_DIR}" )
  else()
    if(BOOST_LIBRARYDIR)
      MESSAGE( STATUS "Boost_LIBRARY_DIR empty but BOOST_LIBRARYDIR is set setting Boost_LIBRARY_DIR to: ${BOOST_LIBRARYDIR}" )
      set(Boost_LIBRARY_DIR ${BOOST_LIBRARYDIR})
    endif()
  endif()
else()
  MESSAGE( STATUS "Boost was not found!")
endif()

# cmake 3.15 introduced a new variable and a new format for the old one
if (DEFINED Boost_VERSION_MACRO)
  set(FTDOIP_BOOST_VERSION ${Boost_VERSION_MACRO})
else()
  set(FTDOIP_BOOST_VERSION ${Boost_VERSION})
endif()

message( STATUS "Using boost version: ${FTDOIP_BOOST_VERSION}" )
if (${FTDOIP_BOOST_VERSION} GREATER 107400)
message( ERROR "boost version ${FTDOIP_BOOST_VERSION} is not (yet) supported. Latest supported version is 1.74.0" )
elseif(${FTDOIP_BOOST_VERSION} GREATER 107300)
set(FTDOIP_BOOST_HELPER include/helper/1.74)
elseif(${FTDOIP_BOOST_VERSION} GREATER 106999)
set(FTDOIP_BOOST_HELPER include/helper/1.70)
elseif(${FTDOIP_BOOST_VERSION} GREATER 106599)
set(FTDOIP_BOOST_HELPER include/helper/1.66)
else()
set(FTDOIP_BOOST_HELPER include/helper/1.55)
endif()

include_directories(
    include
    ${FTDOIP_BOOST_HELPER}
)

set(CMAKE_CXX_FLAGS "-g -std=c++11")

file(GLOB endpoint_src
    "src/*.cpp"
)

add_library(endpoint SHARED ${endpoint_src})