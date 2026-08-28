# SPDX-License-Identifier: Apache-2.0

get_filename_component(MESHCORE_TEST_COMMON_DIR
  "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(FOBE_REPO_ROOT
  "${MESHCORE_TEST_COMMON_DIR}/../../../.." ABSOLUTE)

include("${MESHCORE_TEST_COMMON_DIR}/meshcore_module.cmake")
set(MESHCORE_REPO_ROOT "${MESHCORE_LIB_DIR}")

set(MESHCORE_REFERENCE_DIR
  "${ZEPHYR_MESHCORE_MODULE_DIR}/.reference/meshcore/src")
if(NOT EXISTS "${MESHCORE_REFERENCE_DIR}/Dispatcher.cpp")
  set(MESHCORE_REFERENCE_DIR "${FOBE_REPO_ROOT}/.reference/meshcore/src")
endif()
if(NOT EXISTS "${MESHCORE_REFERENCE_DIR}/Dispatcher.cpp")
  message(FATAL_ERROR
    "MeshCore upstream reference not found. Expected "
    "${ZEPHYR_MESHCORE_MODULE_DIR}/.reference/meshcore/src or "
    "${FOBE_REPO_ROOT}/.reference/meshcore/src"
  )
endif()

set(MESHCORE_REFERENCE_INCLUDE_DIRS
  "${MESHCORE_REFERENCE_DIR}"
  "${MESHCORE_REFERENCE_DIR}/helpers"
  "${MESHCORE_TEST_COMMON_DIR}/include"
)

set(MESHCORE_REFERENCE_PACKET_SOURCES
  "${MESHCORE_REFERENCE_DIR}/Packet.cpp"
)
set(MESHCORE_REFERENCE_UTILS_SOURCES
  "${MESHCORE_REFERENCE_DIR}/Utils.cpp"
)
set(MESHCORE_REFERENCE_IDENTITY_SOURCES
  "${MESHCORE_REFERENCE_DIR}/Identity.cpp"
)
set(MESHCORE_REFERENCE_DISPATCHER_SOURCES
  "${MESHCORE_REFERENCE_DIR}/Dispatcher.cpp"
)
set(MESHCORE_REFERENCE_MESH_SOURCES
  "${MESHCORE_REFERENCE_DIR}/Mesh.cpp"
)
set(MESHCORE_REFERENCE_STATIC_POOL_PACKET_MANAGER_SOURCES
  "${MESHCORE_REFERENCE_DIR}/helpers/StaticPoolPacketManager.cpp"
)
set(MESHCORE_REFERENCE_ADVERT_DATA_SOURCES
  "${MESHCORE_REFERENCE_DIR}/helpers/AdvertDataHelpers.cpp"
)

set(MESHCORE_REFERENCE_CRYPTO_SOURCES
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Crypto.cpp"
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Hash.cpp"
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/SHA256.cpp"
)

set(MESHCORE_REFERENCE_CRYPTO_AES_SOURCES
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/BlockCipher.cpp"
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/AESCommon.cpp"
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/AES128.cpp"
)

set(MESHCORE_REFERENCE_CRYPTO_IDENTITY_SOURCES
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/SHA512.cpp"
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/BigNumberUtil.cpp"
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Curve25519.cpp"
  "${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Ed25519.cpp"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/add_scalar.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/fe.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/ge.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/key_exchange.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/keypair.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sc.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/seed.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sha512.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sign.c"
  "${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/verify.c"
)

# Upstream headers assume Arduino-style transitive includes and share protocol
# macro names with the C port. Keep this scoped to C++ test translation units.
zephyr_library_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:-include>
  $<$<COMPILE_LANGUAGE:CXX>:${MESHCORE_TEST_COMMON_DIR}/include/meshcore_reference_preinclude.h>
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-error>
)
