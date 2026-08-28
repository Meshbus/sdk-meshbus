#include <zephyr/ztest.h>

extern "C" {
#include "meshcore/platform.h"
#include "meshcore/runtime.h"
#include "meshcore/types.h"

unsigned int meshcore_public_abi_c_compile_probe(void);
}

#if MESHCORE_ABI_VERSION != 28U
#error "Meshbus SDK integration requires MeshCore ABI 28"
#endif

ZTEST(meshcore_public_abi, test_public_headers_compile_as_c_and_cpp)
{
	zassert_equal(MESHCORE_ABI_VERSION, 28U);
	zassert_equal(MESHCORE_PUBLIC_KEY_SIZE, 32U);
	zassert_equal(MESHCORE_CHANNEL_SECRET_MAX_LEN, 32U);
	zassert_equal(MESHCORE_MAX_PATH_LEN, 64U);
	zassert_true(meshcore_public_abi_c_compile_probe() > 0U);
}

ZTEST_SUITE(meshcore_public_abi, NULL, NULL, NULL, NULL, NULL);
