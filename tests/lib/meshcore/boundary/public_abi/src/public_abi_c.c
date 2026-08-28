#include "meshcore/platform.h"
#include "meshcore/runtime.h"
#include "meshcore/types.h"

#if MESHCORE_ABI_VERSION != 28U
#error "Meshbus SDK integration requires MeshCore ABI 28"
#endif

unsigned int meshcore_public_abi_c_compile_probe(void)
{
	meshcore_common_peer_path_t peer_path = { 0 };

	peer_path.has_out_path = false;
	peer_path.out_path_byte_len = 0U;

	return MESHCORE_ABI_VERSION + MESHCORE_PUBLIC_KEY_SIZE +
	       MESHCORE_CHANNEL_SECRET_MAX_LEN + peer_path.path_hash_size;
}
