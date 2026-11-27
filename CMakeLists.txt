cmake_minimum_required(VERSION 3.16)

project(NeoPostOffice
  VERSION 0.9.5
  DESCRIPTION "X11 mail client, modernized from PostOffice 0.7.3"
  LANGUAGES C CXX)

include(GNUInstallDirs)
# Documentation goes in share/doc/neo-postoffice (GNUInstallDirs would name
# it after the CMake project, NeoPostOffice).
set(PO_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/neo-postoffice" CACHE PATH "Where README.md and docs/ are installed")
if(IS_ABSOLUTE "${PO_DOCDIR}")
  set(PO_FULL_DOCDIR "${PO_DOCDIR}")
else()
  set(PO_FULL_DOCDIR "${CMAKE_INSTALL_PREFIX}/${PO_DOCDIR}")
endif()

set(WITH_POSTGRESQL AUTO CACHE STRING
    "PostgreSQL mail storage: AUTO (if libpq is found), ON or OFF")
set_property(CACHE WITH_POSTGRESQL PROPERTY STRINGS AUTO ON OFF)
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

# FLTK 1.4: prefer fltk-config, fall back to FLTK's CMake package.
# (Debian 13's FLTKConfig.cmake is broken: it defines fltk::fluid twice and
# references a /usr/bin/fluid-shared that the package does not install, which
# is a fatal error even for find_package(... QUIET). fltk-config works.)
find_program(FLTK_CONFIG fltk-config)
if(NOT FLTK_CONFIG)
  find_package(FLTK 1.4 CONFIG REQUIRED)
  message(STATUS "Using FLTK ${FLTK_VERSION} (CMake package)")
  set(PO_FLTK_LIBS fltk::images fltk::fltk)
else()
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

  # fltk-config names the libraries FLTK needs (-lX11, -lXft, -lpng, ...)
  # without saying where they are. On Linux they are in the default places;
  # on the BSDs they are in /usr/local/lib, /usr/pkg/lib or /usr/X11R7/lib,
  # which the linker and the run-time loader must be told (0.9.5).
  get_filename_component(_po_x11_libdir "${X11_X11_LIB}" DIRECTORY)
  get_filename_component(_po_fltk_libdir "${FLTK_CONFIG}" DIRECTORY)
  get_filename_component(_po_fltk_libdir "${_po_fltk_libdir}/../lib" ABSOLUTE)
  set(PO_EXTRA_LIBDIRS "${_po_x11_libdir}" "${_po_fltk_libdir}")
  foreach(_po_prefix IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND PO_EXTRA_LIBDIRS "${_po_prefix}/lib")
  endforeach()
  if(CMAKE_SYSTEM_NAME MATCHES "BSD|DragonFly")
    list(APPEND PO_EXTRA_LIBDIRS /usr/local/lib)
  endif()
  list(REMOVE_DUPLICATES PO_EXTRA_LIBDIRS)
  set(_po_libdirs)
  foreach(_po_dir IN LISTS PO_EXTRA_LIBDIRS)
    if(IS_DIRECTORY "${_po_dir}" AND NOT _po_dir IN_LIST CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES
       AND NOT _po_dir MATCHES "^/(usr/)?lib(64|32)?(/.*-linux-gnu)?$")
      list(APPEND _po_libdirs "${_po_dir}")
    endif()
  endforeach()
  if(_po_libdirs)
    target_link_directories(po_fltk INTERFACE ${_po_libdirs})
    list(APPEND CMAKE_BUILD_RPATH ${_po_libdirs})
    list(APPEND CMAKE_INSTALL_RPATH ${_po_libdirs})
    message(STATUS "Library directories for FLTK's libraries: ${_po_libdirs}")
  endif()

  message(STATUS "Using FLTK ${PO_FLTK_VERSION} (${FLTK_CONFIG})")
  set(PO_FLTK_LIBS po_fltk)
endif()

# OpenSSL (or LibreSSL on OpenBSD) for TLS to mail servers (Debian: libssl-dev)
find_package(OpenSSL 1.1 REQUIRED)

find_path(GDBM_INCLUDE_DIR gdbm.h REQUIRED)
find_library(GDBM_LIBRARY gdbm REQUIRED)

# PostgreSQL client library (Debian: libpq-dev)
set(PO_POSTGRESQL OFF)
if(NOT WITH_POSTGRESQL STREQUAL "OFF")
  find_path(PQ_INCLUDE_DIR libpq-fe.h PATH_SUFFIXES postgresql pgsql)
  find_library(PQ_LIBRARY pq)
  if(PQ_INCLUDE_DIR AND PQ_LIBRARY)
    set(PO_POSTGRESQL ON)
    message(STATUS "PostgreSQL support: enabled (${PQ_LIBRARY})")
  elseif(WITH_POSTGRESQL STREQUAL "ON")
    message(FATAL_ERROR "WITH_POSTGRESQL=ON but libpq was not found (install libpq-dev)")
  else()
    message(STATUS "PostgreSQL support: disabled (libpq not found; install libpq-dev)")
  endif()
endif()

# libetpan for IMAP (Debian: libetpan-dev; BSD ports: mail/libetpan).
# Without it IMAP mailboxes are shown as unavailable.
set(WITH_LIBETPAN "AUTO" CACHE STRING "IMAP support with libetpan: AUTO, ON or OFF")
set(PO_LIBETPAN OFF)
if(NOT WITH_LIBETPAN STREQUAL "OFF")
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBETPAN QUIET IMPORTED_TARGET libetpan>=1.9)
  endif()
  if(LIBETPAN_FOUND)
    set(PO_LIBETPAN ON)
    message(STATUS "IMAP support: enabled (libetpan ${LIBETPAN_VERSION})")
  elseif(WITH_LIBETPAN STREQUAL "ON")
    message(FATAL_ERROR "WITH_LIBETPAN=ON but libetpan was not found (install libetpan-dev)")
  else()
    message(WARNING "IMAP support: disabled, libetpan was not found. Install libetpan-dev "
                    "(Debian; mail/libetpan in the BSD ports) and pkg-config, then run cmake again.")
  endif()
else()
  message(STATUS "IMAP support: disabled (WITH_LIBETPAN=OFF)")
endif()

# ---------------------------------------------------------------------------
# Common compile settings
# ---------------------------------------------------------------------------

add_library(po_settings INTERFACE)
target_compile_definitions(po_settings INTERFACE _REENTRANT USE_GDBM)
if(PO_POSTGRESQL)
  target_compile_definitions(po_settings INTERFACE PO_HAVE_POSTGRESQL)
endif()
if(PO_LIBETPAN)
  target_compile_definitions(po_settings INTERFACE PO_HAVE_LIBETPAN)
endif()
if(PO_WARNINGS)
  target_compile_options(po_settings INTERFACE -Wall -Wextra)
endif()
# FLTK's headers include <X11/Xlib.h>; on the BSDs that is not in
# /usr/include, so every target gets X11's include directory.
target_link_libraries(po_settings INTERFACE ${PO_FLTK_LIBS} X11::X11)

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
  mailboxtree.cpp attachments.cpp flowed.cpp redirect.cpp print.cpp search.cpp addrbook.cpp addrwin.cpp prefsave.cpp mdi.cpp prefpanel.cpp style.cpp)

set(PO_BACKEND_SOURCES
  base64.cpp btree.cpp buffer.cpp ccache.cpp checksum.cpp chop.cpp
  connection.cpp debug.cpp eaddr.cpp encode.cpp envelope.cpp folderxpm.cpp
  formatter.cpp gdbmstorage.cpp gdbmtools.cpp itc.cpp
  mailbox.cpp mailerthread.cpp mailmessage.cpp mailstorage.cpp mailview.cpp
  localmailhandler.cpp mailhandler.cpp maildirhandler.cpp netstream.cpp certstore.cpp sendpipe.cpp mhhandler.cpp mailheaders.cpp mboxhandler.cpp mboxstorage.cpp
  podb.cpp podb_pgsql.cpp pgstorage.cpp
  imap4handler.cpp outboxhandler.cpp pop3handler.cpp posixthread.cpp prefjournal.cpp prefs.cpp
  reg.cpp scheduler.cpp semaphore.cpp smtpclient.cpp status.cpp storagelist.cpp
  strdupp.cpp stringtoken.cpp substr.cpp tolower.cpp usleep.cpp var.cpp
  var2.cpp version.cpp wizard.cpp wordlist.cpp searchcore.cpp htmltext.cpp)

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
  ${GDBM_LIBRARY} OpenSSL::SSL OpenSSL::Crypto X11::X11 X11::Xext Threads::Threads ${CMAKE_DL_LIBS} m)
if(PO_POSTGRESQL)
  target_include_directories(postoffice PRIVATE ${PQ_INCLUDE_DIR})
  target_link_libraries(postoffice PRIVATE ${PQ_LIBRARY})
endif()
if(PO_LIBETPAN)
  target_link_libraries(postoffice PRIVATE PkgConfig::LIBETPAN)
endif()

# ---------------------------------------------------------------------------
# Installing (0.9.5): the program, its man page, the desktop menu entry and
# icon, and the documentation. DESTDIR works for packages:
#   DESTDIR=/tmp/pkg cmake --install build
# ---------------------------------------------------------------------------

install(TARGETS postoffice RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The date in the man page is the build date (SOURCE_DATE_EPOCH is honoured
# for reproducible builds).
string(TIMESTAMP PO_MAN_DATE "%Y-%m-%d")
configure_file(data/postoffice.1 ${CMAKE_CURRENT_BINARY_DIR}/postoffice.1 @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/postoffice.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

install(FILES data/neo-postoffice.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/neo-postoffice.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
foreach(size 16 22 24 32 48 64 128 256)
  install(FILES data/icons/neo-postoffice-${size}.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
          RENAME neo-postoffice.png)
endforeach()

install(FILES README.md LICENSE DESTINATION ${PO_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${PO_DOCDIR})

