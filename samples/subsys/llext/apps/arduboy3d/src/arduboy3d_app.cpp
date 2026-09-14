/* SPDX-License-Identifier: MIT */
#include <meshbus_arduboy/runtime.hpp>
#include <zephyr/llext/symbol.h>
#include <zephyr/sys/printk.h>
void setup();
void loop();
extern "C" void arduboy3d_app_main(void *args) {
    meshbus::arduboy::SketchConfig config{"arduboy3d", setup, loop};
    int rc=meshbus::arduboy::run_sketch(args, config);
    printk("[arduboy3d] complete rc=%d\n",rc);
}
LL_EXTENSION_SYMBOL(arduboy3d_app_main);
