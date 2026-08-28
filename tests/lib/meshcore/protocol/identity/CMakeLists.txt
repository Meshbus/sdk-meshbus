cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshcore_identity_tdd_test)

include(${CMAKE_CURRENT_LIST_DIR}/../../common/reference_meshcore.cmake)

zephyr_include_directories(
  ${MESHCORE_REFERENCE_INCLUDE_DIRS}
  ${MESHCORE_INTERNAL_INCLUDE_DIRS}
  ${MESHCORE_TEST_COMMON_DIR}/include
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519
)

target_sources(app PRIVATE
  src/main.cpp
  src/rng_stub.cpp
  ${MESHCORE_TEST_COMMON_DIR}/src/meshcore_hal_zephyr.c
  ${MESHCORE_PLATFORM_BRIDGE_SOURCES}
  ${MESHCORE_PROTOCOL_RNG_SOURCES}
  ${MESHCORE_PROTOCOL_UTILS_SOURCES}
  ${MESHCORE_PROTOCOL_IDENTITY_SOURCES}
  ${MESHCORE_PROTOCOL_CRYPTO_SOURCES}
  ${MESHCORE_REFERENCE_UTILS_SOURCES}
  ${MESHCORE_REFERENCE_IDENTITY_SOURCES}
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Crypto.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Hash.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/SHA256.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/SHA512.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/BlockCipher.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/AESCommon.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/AES128.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/BigNumberUtil.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Curve25519.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/Crypto/Ed25519.cpp
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/add_scalar.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/fe.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/ge.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/key_exchange.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/keypair.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sc.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/seed.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sha512.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/sign.c
  ${MESHCORE_TEST_COMMON_DIR}/lib/ed25519/verify.c
)

zephyr_library_compile_options(
  -Wno-unused-parameter
  -Wno-unused-variable
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
)
