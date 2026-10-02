# Local changes applied to the bundled librats sources (src/librats) before they
# are configured:
#
#   * The per-handshake "noise" messages are logged at DEBUG instead of INFO. A
#     node that is reachable from the internet receives a steady stream of inbound
#     handshakes and these four lines per handshake used to dominate the log.
#   * Inbound handshakes are counted per remote address and summarised once a
#     minute under the "inbound" log tag (see cmake/librats/inbound_stats.h).
#
# Every edit replaces one exact source line and is skipped when the file already
# carries the result, so re-running CMake changes nothing. Single-line anchors
# match regardless of the checkout's line endings.

set(RATS_LIBRATS_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/librats")
get_filename_component(RATS_LIBRATS_DIR "${RATS_LIBRATS_DIR}" ABSOLUTE)

function(rats_librats_replace relpath old new)
    set(path "${RATS_LIBRATS_DIR}/${relpath}")
    file(READ "${path}" content)
    string(FIND "${content}" "${new}" done_at)
    if(NOT done_at EQUAL -1)
        return()
    endif()
    string(FIND "${content}" "${old}" anchor_at)
    if(anchor_at EQUAL -1)
        message(FATAL_ERROR "librats patch: anchor not found in ${relpath}:\n  ${old}")
    endif()
    string(REPLACE "${old}" "${new}" content "${content}")
    file(WRITE "${path}" "${content}")
    message(STATUS "librats patch: updated ${relpath}")
endfunction()

if(NOT EXISTS "${RATS_LIBRATS_DIR}/src/librats/transport/connection.cpp")
    message(FATAL_ERROR "librats sources not found in ${RATS_LIBRATS_DIR} (submodule not checked out?)")
endif()

# configure_file only rewrites the copy when its content changes.
configure_file("${CMAKE_CURRENT_LIST_DIR}/librats/inbound_stats.h"
               "${RATS_LIBRATS_DIR}/src/librats/transport/inbound_stats.h" COPYONLY)

rats_librats_replace(src/librats/crypto/noise.cpp
    [=[#define LOG_NOISE_INFO(message)  LOG_INFO("noise", message)]=]
    [=[#define LOG_NOISE_INFO(message)  LOG_DEBUG("noise", message)]=])

rats_librats_replace(src/librats/transport/connection.cpp
    [=[#include "librats/util/logger.h"]=]
    [=[#include "librats/util/logger.h"
#include "librats/transport/inbound_stats.h"]=])

rats_librats_replace(src/librats/transport/connection.cpp
    [=[    handshaker_ = reactor_.security().create(role_);]=]
    [=[    handshaker_ = reactor_.security().create(role_);
    if (role_ == ConnRole::Inbound) inbound_stats::handshake_started(remote_endpoint());]=])

rats_librats_replace(src/librats/transport/connection.cpp
    [=[        handshaker_.reset();]=]
    [=[        handshaker_.reset();
        if (role_ == ConnRole::Inbound) inbound_stats::handshake_completed(remote_endpoint());]=])
