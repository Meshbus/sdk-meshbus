# `oracle_parity` Test Cases

## 定位

`sdk-meshbus/tests/lib/meshcore/runtime/oracle` 是 `lib-mc`
oracle parity test suite 的用例索引。

- 编译进 harness 的 oracle source：`.reference/meshcore/src`
- runtime 行为证据还包括 `.reference/meshcore/examples/companion_radio`；
  具体 API 到 evidence 的追踪见
  `tests/lib/meshcore/runtime/runtime_api_map.json`
- 对比标准：
  - send-side：语义等价请求下，outbound raw packet 与可观察发送行为一致
  - receive-side：语义等价注入下，publish/observe 行为与 oracle 一致
- 不替代 `sdk-meshbus/tests/lib/meshcore/runtime/requests`
  - `meshcore_runtime_test` 继续负责 ABI 契约、参数校验、容量与错误码

## Suite Summary

| Item | Value |
| --- | --- |
| Suite | `meshcore_runtime_oracle` |
| Harness | `ztest` |
| Platform | `qemu_x86` |
| Current `ZTEST` count | `43` |
| Send-side target ABI parity tests | `15` |
| Receive-side parity tests | `19` |
| Negative parity tests | `9` |
| Oracle smoke tests | `1` |

## Maintenance Guard

在修改 `src/*.cpp` 或本文件后，建议执行：

```sh
west build -b qemu_x86 sdk-meshbus/tests/lib/meshcore/runtime/oracle -d build/oracle_parity
west build -d build/oracle_parity -t oracle_parity_check_test_cases
```

该目标会校验：

- `src/*.cpp` 中 `ZTEST(meshcore_runtime_oracle, ...)` 聚合列表
- 本文件 `Current Implemented ZTEST Cases` 表格
- `Suite Summary` 中 `Current ZTEST count`

三者是否一致。

同一个校验也会在本 suite 的 CMake configure 阶段运行，因此普通
Twister 执行会直接拦截 stale `test_cases.md`。

## Current Implemented ZTEST Cases

| ZTEST | Target ABI / Surface | Oracle Helper | Primary Goal | Key Assertions |
| --- | --- | --- | --- | --- |
| `test_reference_harness_smoke_emits_raw_advert` | 无 target ABI，oracle smoke | `ReferenceChatHarness::send_self_advert(false)` | 验证 oracle harness 在最小 fixture 下能产出可解析 outbound raw advert | 发包成功；`route=zero-hop`；`payload=ADVERT` |
| `test_node_advert_local_matches_oracle` | `meshcore_node_advert_request(false)` | `send_self_advert(false)` | 本地 advert zero-hop raw parity | raw bytes 一致；`route=zero-hop`；`payload=ADVERT` |
| `test_node_advert_flood_matches_oracle` | `meshcore_node_advert_request(true)` | `send_self_advert(true)` | flood advert raw parity | raw bytes 一致；`route=flood`；`payload=ADVERT` |
| `test_peer_advert_replay_matches_oracle` | `meshcore_node_peer_advert_request(raw, raw_len)` | `replay_peer_advert_zero_hop(raw, raw_len)` | peer advert replay 后 zero-hop outbound parity | raw bytes 一致；`route=zero-hop`；`payload=ADVERT` |
| `test_send_to_node_direct_matches_oracle` | `meshcore_message_send_to_node(..., flood=false, attempt=1, ...)` | `send_message_to_contact(..., false, 1, ...)` | 有 out-path 时 direct 文本消息 parity | raw bytes 一致；`route=direct`；`payload=TXT_MSG` |
| `test_send_to_node_flood_attempt_tail_matches_oracle` | `meshcore_message_send_to_node(..., flood=true, attempt=7, ...)` | `send_message_to_contact(..., true, 7, ...)` | flood 发送与 `attempt > 3` 尾部编码 parity | raw bytes 一致；`route=flood`；`payload=TXT_MSG` |
| `test_send_to_channel_matches_oracle` | `meshcore_message_send_to_channel(secret, len, payload, len)` | `send_group_message(...)` | group flood 发送 parity，包括 sender-name 前缀与最大 payload 截断行为 | raw bytes 一致；`route=flood`；`payload=GRP_TXT` |
| `test_channel_data_flood_matches_oracle` | `meshcore_channel_data_send(secret, len, NULL, OUT_PATH_UNKNOWN, data_type, payload, len)` | `send_group_data(..., path_len=OUT_PATH_UNKNOWN, data_type, payload, len)` | group/channel binary datagram flood parity | raw bytes 一致；`route=flood`；`payload=GRP_DATA` |
| `test_channel_data_direct_matches_oracle` | `meshcore_channel_data_send(secret, len, path, path_len, data_type, payload, len)` | `send_group_data(..., path, path_len, data_type, payload, len)` | group/channel binary datagram direct parity | raw bytes 一致；`route=direct`；`payload=GRP_DATA` |
| `test_no_channel_send_group_emits_no_outbound_like_oracle` | `meshcore_message_send_to_channel(secret, len, payload, len)` with unknown channel secret | `send_group_message(...)` with known-channel guard | no-channel negative parity | 两边都不发包；不比较返回码 |
| `test_zz_encrypted_path_ack_extra_keeps_discover_pending_like_oracle` | `meshcore_node_discover_path_request(pub, NULL)` + encrypted inbound `PATH` with `ACK` extra | `send_discover_request(...)` + `build_inbound_path_raw(..., extra=ACK)` + `inject_raw_packet(...)` | 加密有效但语义非法 `PATH` 变体（ACK extra）不应命中 discover pending | 两边都不 publish peer-path；discover pending 保持有效；无 follow-up outbound |
| `test_discover_path_plain_matches_oracle` | `meshcore_node_discover_path_request(pub, NULL)` | `send_discover_request(pub)` | 普通 discover request flood parity | raw bytes 一致；`route=flood`；无 transport codes；`payload=REQ` |
| `test_no_local_identity_discover_emits_no_outbound_like_oracle` | `meshcore_node_discover_path_request(pub, NULL)` without local identity | `send_discover_request(pub)` without self identity | no-local-identity negative parity | 两边都不发包；不比较返回码 |
| `test_trace_path_matches_oracle` | `meshcore_node_trace_request(route, len, hash_size, NULL)` | `send_trace_request(pub)` | 普通 direct trace parity | raw bytes 一致；`route=direct`；`payload=TRACE` |
| `test_trace_path_hash_size_3_downgrades_like_oracle` | `meshcore_node_trace_request(route, len, hash_size, NULL)` with source path hash size `3` | `send_trace_request(pub)` with oracle path hash size `3` | `hash-size 3 -> 2` downgrade parity | raw bytes 一致；`route=direct`；`payload=TRACE` |
| `test_no_path_trace_emits_no_outbound_like_oracle` | deprecated `meshcore_node_trace_path_request(pub, NULL)` without out-path | `send_trace_request(pub)` without out-path | no-path negative parity | 两边都不发包；不创建 pending trace；不比较返回码 |
| `test_telemetry_request_matches_oracle` | `meshcore_node_telemetry_request(pub, permission_mask, NULL)` | `send_telemetry_request(pub, permission_mask)` | telemetry request flood parity | raw bytes 一致；`route=flood`；`payload=REQ` |
| `test_binary_request_flood_matches_oracle` | `meshcore_node_binary_request(pub, payload, len)` without out-path | `send_binary_request(pub, payload, len)` | generic binary request flood parity | raw bytes 一致；`route=flood`；`payload=REQ` |
| `test_binary_request_direct_matches_oracle` | `meshcore_node_binary_request(pub, payload, len)` with out-path | `send_binary_request(pub, payload, len)` | generic binary request direct parity | raw bytes 一致；`route=direct`；`payload=REQ` |
| `test_discover_response_publish_matches_oracle` | `meshcore_node_discover_path_request(pub, NULL)` after pending response hit | `send_discover_request(pub)` + `inject_discover_response(...)` | discover response publish parity | peer-path publish 一致；`is_discover=true`；`out_path` 一致；pending cleared |
| `test_discover_response_without_snr_publish_matches_oracle` | `meshcore_node_discover_path_request(pub, NULL)` after pending response hit | `send_discover_request(pub)` + `inject_discover_response(...)` | discover plain response publish parity | peer-path publish 一致；`is_discover=true`；`out_path` 一致；无 SNR publish；pending cleared |
| `test_discover_response_wrong_peer_is_ignored_like_oracle` | `meshcore_node_discover_path_request(pub, NULL)` after mismatched peer-path response | `send_discover_request(pub)` + `inject_discover_response_for(...)` | discover wrong-peer ignore parity | 两边都不 publish；pending discovery 保持有效 |
| `test_discover_duplicate_response_after_completion_is_ignored_like_oracle` | `meshcore_node_discover_path_request(pub, NULL)` after duplicate path response | `send_discover_request(pub)` + `inject_discover_response(...)` / `inject_discover_response_for(...)` | discover duplicate-response ignore parity | 首次 publish 一致；重复注入后两边都不 republish；pending 保持 cleared |
| `test_discover_no_response_timeout_clears_pending_like_oracle` | `meshcore_node_discover_path_request(pub, NULL)` without any response | `send_discover_request(pub)` + oracle time advance | discover no-response timeout parity | 两边都不 publish；pending discovery 在超时后清理 |
| `test_discover_same_surface_overwrites_like_oracle` | two consecutive `meshcore_node_discover_path_request(...)` calls on the same surface | `send_discover_request(...)` twice | same-surface overwrite parity | both accept request 2, ignore request 1's stale response, and correlate request 2 |
| `test_trace_result_publish_matches_oracle` | `meshcore_node_trace_request(route, len, hash_size, NULL)` after pending trace hit | `send_trace_request(pub)` + `inject_trace_result(...)` | trace result publish parity | trace publish 一致；`state=1`；`out_path_snr`/`return_path_snr` 一致；pending cleared |
| `test_trace_invalid_result_clears_pending_like_oracle` | `meshcore_node_trace_request(route, len, hash_size, NULL)` after invalid trace result | `send_trace_request(pub)` + `inject_trace_result_for(...)` | malformed matched-result consumption parity | 两边都不 publish，并清空对应 pending |
| `test_trace_wrong_tag_is_ignored_like_oracle` | `meshcore_node_trace_request(route, len, hash_size, NULL)` after wrong-tag trace result | `send_trace_request(pub)` + `inject_trace_result_for(...)` | trace wrong-tag ignore parity | 两边都不 publish；pending trace 保持有效 |
| `test_trace_duplicate_result_after_completion_is_ignored_like_oracle` | `meshcore_node_trace_request(route, len, hash_size, NULL)` after duplicate trace result | `send_trace_request(pub)` + `inject_trace_result(...)` / `inject_trace_result_for(...)` | trace duplicate-result ignore parity | 首次 publish 一致；重复注入后两边都不 republish；pending 保持 cleared |
| `test_trace_same_surface_overwrites_like_oracle` | two consecutive `meshcore_node_trace_request(...)` calls on the same surface | `send_trace_request(...)` twice | same-surface overwrite parity | both accept request 2, ignore request 1's stale response, and correlate request 2 |
| `test_telemetry_response_publish_matches_oracle` | `meshcore_node_telemetry_request(pub, permission_mask, NULL)` after pending response hit | `send_telemetry_request(pub, permission_mask)` + `inject_telemetry_response(...)` | telemetry response publish parity | telemetry publish 一致；`key_prefix` 一致；payload 一致；pending cleared |
| `test_telemetry_response_wrong_peer_is_ignored_like_oracle` | `meshcore_node_telemetry_request(pub, permission_mask, NULL)` after wrong-peer response | `send_telemetry_request(pub, permission_mask)` + `inject_telemetry_response_for(...)` | telemetry wrong-peer ignore parity | 两边都不 publish；pending telemetry 保持有效 |
| `test_telemetry_response_wrong_tag_is_ignored_like_oracle` | `meshcore_node_telemetry_request(pub, permission_mask, NULL)` after wrong-tag response | `send_telemetry_request(pub, permission_mask)` + `inject_telemetry_response_for(...)` | telemetry wrong-tag ignore parity | 两边都不 publish；pending telemetry 保持有效 |
| `test_telemetry_duplicate_response_after_completion_is_ignored_like_oracle` | `meshcore_node_telemetry_request(pub, permission_mask, NULL)` after duplicate response | `send_telemetry_request(pub, permission_mask)` + `inject_telemetry_response(...)` / `inject_telemetry_response_for(...)` | telemetry duplicate-response ignore parity | 首次 publish 一致；重复注入后两边都不 republish；pending 保持 cleared |
| `test_telemetry_same_surface_overwrites_like_oracle` | two consecutive `meshcore_node_telemetry_request(...)` calls on the same surface | `send_telemetry_request(...)` twice | same-surface overwrite parity | both accept request 2, ignore request 1's stale response, and correlate request 2 |
| `test_pending_timeout_clears_all_like_oracle` | combined discover / trace / telemetry pending lifecycle | `send_discover_request(...)` + `send_trace_request(...)` + `send_telemetry_request(...)` + oracle time advance | timeout cleanup parity | 三类 pending 都在超时后被清理；两边都不产生 publish |
| `test_pending_overlap_responses_publish_independently_like_oracle` | concurrent discover / trace / telemetry pending lifecycle | `send_discover_request(...)` + `send_trace_request(...)` + `send_telemetry_request(...)` + out-of-order response injections | overlap pending correlation parity | 三类 pending 同时有效；telemetry -> trace -> discover 乱序响应分别 publish/clear；未命中的其他 pending 保持有效 |
| `test_invalid_inbound_is_ignored_like_oracle` | `meshcore_radio_rx_inject(raw, ...)` with invalid payload | `inject_advert(raw, len)` invalid input | invalid inbound negative parity | 两边都忽略；不发布 advert；不产生 follow-up outbound |
| `test_invalid_inbound_req_is_ignored_like_oracle` | `meshcore_radio_rx_inject(raw, ...)` with invalid `REQ` packet | `inject_raw_packet(raw, len)` invalid input | invalid inbound `REQ` negative parity | 两边都忽略；不发布 advert/peer-path/trace/telemetry；不产生 follow-up outbound |
| `test_invalid_inbound_response_keeps_telemetry_pending_like_oracle` | invalid inbound `RESPONSE` while telemetry pending | `send_telemetry_request(...)` + `inject_raw_packet(raw, len)` | invalid inbound `RESPONSE` negative parity | 两边都不 publish telemetry；pending telemetry 保持有效；不产生 follow-up outbound |
| `test_invalid_inbound_path_is_ignored_like_oracle` | invalid inbound `PATH` while discover pending | `send_discover_request(...)` + `inject_raw_packet(raw, len)` | invalid inbound `PATH` negative parity | 两边都不 publish；discover pending 保持有效；不产生 follow-up outbound |
| `test_invalid_inbound_trace_is_ignored_like_oracle` | invalid inbound `TRACE` while trace pending | `send_trace_request(...)` + `inject_raw_packet(raw, len)` | invalid inbound `TRACE` negative parity | 两边都不 publish；trace pending 保持有效；不产生 follow-up outbound |
| `test_inbound_advert_is_observed_without_followup_like_oracle` | `meshcore_radio_rx_inject(raw, ...)` inbound advert surface | oracle source `send_self_advert(false)` + `inject_advert(raw, len)` | inbound advert 观测 parity，保证两边 advert publish surface 和 no-follow-up 行为一致 | target advert recv count=`1`；oracle advert observe count=`1`；advert event 的 public key/name/type/timestamp/position/path/is_new 一致；两边 follow-up outbound=`0`；oracle 解析出的 public key 与注入 raw 一致 |

## Coverage Summary By ABI

| ABI / Surface | Count |
| --- | ---: |
| `meshcore_node_advert_request` | `2` |
| `meshcore_node_peer_advert_request` | `1` |
| `meshcore_message_send_to_node` | `2` |
| `meshcore_message_send_to_channel` | `2` |
| `meshcore_channel_data_send` | `2` |
| `meshcore_node_discover_path_request` | `11` |
| `meshcore_node_trace_request` | `9` |
| `meshcore_node_telemetry_request` | `8` |
| `meshcore_node_binary_request` | `2` |
| `meshcore_radio_rx_inject` | `7` |
| `oracle harness smoke` | `1` |

## Planned Cases

### Pending Lifecycle Parity

| Planned Phase | Planned Behavior | Why Not In Current Phase |
| --- | --- | --- |
| `Future` | same-surface multi-slot pending correlation stress | 当前 runtime 每个 surface 保留一个 pending 槽，已覆盖同 surface overwrite 和跨 surface overlap；多 slot 语义需要先明确是否进入 C runtime 目标 |

### Negative Parity

| Planned Phase | Planned Behavior | Why Not In Current Phase |
| --- | --- | --- |
| `Future` | invalid inbound deep matrix expansion (encrypted semantic-invalid payload variants, broader type matrix) | 当前已落地首个 encrypted semantic-invalid 变体（ACK-extra PATH）；后续再扩更广 payload 组合矩阵 |

### Deferred / Out Of Scope

| Planned Phase | Planned Behavior | Why Not In Current Phase |
| --- | --- | --- |
| `Deferred` | `raw/control data` oracle parity | C runtime now exposes these low-level packet surfaces, but the primary upstream evidence is `Mesh.cpp` packet construction plus Companion frame shape rather than `BaseChatMesh` request behavior; coverage lives in protocol/runtime request and Meshbus companion tests. |
| `Out of scope` | Contact/Channel business object projection | Contact/Channel 是调用端抽象；`lib/meshcore` 只消费 peer identity/path、channel secret/hash 等协议输入并发布协议事件 |
| `Out of scope` | message pagination storage and UX | Pagination may use binary request/response or channel datagram as an opaque transport, but page schema, persistence, retries, and UI belong in Meshbus or the caller. |
