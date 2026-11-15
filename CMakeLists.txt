cmake_minimum_required(VERSION 3.16)

project(NeoPostOffice
  VERSION 0.8.0
  DESCRIPTION "X11 mail and Usenet client, modernized from PostOffice 0.7.3"
  LANGUAGES C CXX)

include(GNUInstallDirs)

option(WITH_MYSQL "Build the MySQL/MariaDB mail storage back end" OFF)
option(PO_WARNINGS "Compile with -Wall -Wextra" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(X11 REQUIRED)

# FLTK 1.4: prefer its CMake package, fall back to fltk-config.
find_package(FLTK 1.4 CONFIG QUIET)
if(FLTK_FOUND AND TARGET fltk::fltk)
  message(STATUS "Using FLTK ${FLTK_VERSION} (CMake package)")
  set(PO_FLTK_LIBS fltk::images fltk::fltk)
else()
  find_program(FLTK_CONFIG fltk-config REQUIRED)
  execute_process(COMMAND ${FLTK_CONFIG} --version
                  OUTPUT_VARIABLE PO_FLTK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(PO_FLTK_VERSION VERSION_LESS 1.4)
    message(FATAL_ERROR "FLTK 1.4 or newer is required, found ${PO_FLTK_VERSION}")
  endif()
  execute_process(COMMAND ${FLTK_CONFIG} --use-images --cxxflags
                  OUTPUT_VARIABLE PO_FLTK_CXXFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${FLTK_CONFIG} --use-images --ldflags
                  OUTPUT_VARIABLE PO_FLTK_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
  separate_arguments(PO_FLTK_CXXFLAGS UNIX_COMMAND "${PO_FLTK_CXXFLAGS}")
  separate_arguments(PO_FLTK_LDFLAGS UNIX_COMMAND "${PO_FLTK_LDFLAGS}")
  add_library(po_fltk INTERFACE)
  target_compile_options(po_fltk INTERFACE ${PO_FLTK_CXXFLAGS})
  target_link_libraries(po_fltk INTERFACE ${PO_FLTK_LDFLAGS})
  message(STATUS "Using FLTK ${PO_FLTK_VERSION} (${FLTK_CONFIG})")
  set(PO_FLTK_LIBS po_fltk)
endif()

find_path(GDBM_INCLUDE_DIR gdbm.h REQUIRED)
find_library(GDBM_LIBRARY gdbm REQUIRED)

if(WITH_MYSQL)
  find_package(PkgConfig REQUIRED)
  pkg_search_module(MYSQLCLIENT REQUIRED IMPORTED_TARGET libmariadb mysqlclient)
endif()

# ---------------------------------------------------------------------------
# Common compile settings
# ---------------------------------------------------------------------------

add_library(po_settings INTERFACE)
target_compile_definitions(po_settings INTERFACE _REENTRANT LINUX USE_GDBM)
if(NOT WITH_MYSQL)
  target_compile_definitions(po_settings INTERFACE NO_MYSQL)
endif()
if(PO_WARNINGS)
  target_compile_options(po_settings INTERFACE -Wall -Wextra)
endif()
target_link_libraries(po_settings INTERFACE ${PO_FLTK_LIBS})

# ---------------------------------------------------------------------------
# Bundled widget libraries
# ---------------------------------------------------------------------------

# Fl_Toggle_Tree (Maarten de Boer / James Dean Palmer)
add_library(toggletree STATIC
  src/toggletree/Fl_Toggle_Node.cxx
  src/toggletree/Fl_Toggle_Node_Base.cxx
  src/toggletree/Fl_Toggle_Tree.cxx
  src/toggletree/Fl_Toggle_Tree_Base.cxx)
target_include_directories(toggletree PUBLIC src/toggletree)
target_link_libraries(toggletree PRIVATE po_settings)

# Fl_Editor (Robert Kesterson): librcl text engine + librcledit widgets
file(GLOB RCL_SOURCES CONFIGURE_DEPENDS
  src/fl_editor/librcl/src/editor/*.cpp
  src/fl_editor/librcl/src/misc/*.cpp
  src/fl_editor/librcl/src/string/*.cpp)
add_library(rcl STATIC ${RCL_SOURCES})
target_include_directories(rcl PUBLIC src/fl_editor/include)
target_link_libraries(rcl PRIVATE po_settings)

add_library(rcledit STATIC
  src/fl_editor/librcledit/src/Fl_Editor.cxx
  src/fl_editor/librcledit/src/Fl_FancyEditor.cxx
  src/fl_editor/librcledit/src/Fl_FancyMultiEditor.cxx
  src/fl_editor/librcledit/src/Fl_MultiEditor.cxx
  src/fl_editor/librcledit/src/Fl_ProgressBox.cxx
  src/fl_editor/librcledit/src/Fl_StatusPanel.cxx)
target_link_libraries(rcledit PUBLIC rcl PRIVATE po_settings)

# ---------------------------------------------------------------------------
# PostOffice
# ---------------------------------------------------------------------------

string(TIMESTAMP PO_BUILD_DATE "%Y%m%d")
string(TIMESTAMP PO_BUILD_TIMESTAMP "%Y/%m/%d")
set(PO_ARCH "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_CXX_COMPILER_ID}${CMAKE_CXX_COMPILER_VERSION}")
string(TOUPPER "${CMAKE_BUILD_TYPE}" _bt)
set(PO_CXXFLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_bt}}")
string(STRIP "${PO_CXXFLAGS}" PO_CXXFLAGS)
configure_file(src/postoffice/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

set(PO_GUI_SOURCES
  composer.cpp filereq.cpp filereq-main.cpp helpdesk.cpp helpdesk-main.cpp
  ht.cpp iconbrowser.cpp initexitthread.cpp listviewtree.cpp maccheck.cpp
  mailboxtree.cpp mdi.cpp prefpanel.cpp style.cpp)

set(PO_BACKEND_SOURCES
  base64.cpp btree.cpp buffer.cpp ccache.cpp checksum.cpp chop.cpp
  connection.cpp debug.cpp eaddr.cpp encode.cpp envelope.cpp folderxpm.cpp
  formatter.cpp gdbmstorage.cpp gdbmtools.cpp imap4handler.cpp itc.cpp
  mailbox.cpp mailerthread.cpp mailmessage.cpp mailstorage.cpp mailview.cpp
  mboxhandler.cpp mboxstorage.cpp mysqlhandler.cpp nntphandler.cpp
  outboxhandler.cpp pop3handler.cpp posixthread.cpp prefjournal.cpp prefs.cpp
  reg.cpp scheduler.cpp semaphore.cpp smtp.cpp status.cpp storagelist.cpp
  strdupp.cpp stringtoken.cpp substr.cpp tolower.cpp usleep.cpp var.cpp
  var2.cpp version.cpp wizard.cpp wordlist.cpp)

if(WITH_MYSQL)
  list(APPEND PO_BACKEND_SOURCES mysql.cpp)
endif()

list(TRANSFORM PO_GUI_SOURCES PREPEND src/postoffice/)
list(TRANSFORM PO_BACKEND_SOURCES PREPEND src/postoffice/)

add_executable(postoffice ${PO_GUI_SOURCES} ${PO_BACKEND_SOURCES})
target_include_directories(postoffice PRIVATE
  include
  src/postoffice
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${GDBM_INCLUDE_DIR})
target_link_libraries(postoffice PRIVATE
  po_settings toggletree rcledit
  ${GDBM_LIBRARY} X11::X11 X11::Xext Threads::Threads ${CMAKE_DL_LIBS} m)
if(WITH_MYSQL)
  target_link_libraries(postoffice PRIVATE PkgConfig::MYSQLCLIENT)
endif()

install(TARGETS postoffice RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
