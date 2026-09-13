/* SPDX-License-Identifier: MIT */
#include <meshbus_arduboy/runtime.hpp>
#include <zephyr/llext/symbol.h>
#include <zephyr/sys/printk.h>
void setup();
void loop();
extern "C" { int __heap_start; int *__brkval; }
extern "C" void castleboy_app_main(void *args) {
    meshbus::arduboy::SketchConfig config{"castleboy", setup, loop};
    int rc=meshbus::arduboy::run_sketch(args, config);
    printk("[castleboy] complete rc=%d\n",rc);
}
LL_EXTENSION_SYMBOL(castleboy_app_main);
