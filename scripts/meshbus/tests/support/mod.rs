// SPDX-License-Identifier: Apache-2.0
use std::{fs, path::Path};

pub fn public_headers(root: &Path) {
    for module in [
        "bluetooth",
        "channel",
        "clock",
        "contact",
        "desktop",
        "display",
        "firmware",
        "fs",
        "gnss",
        "indicator",
        "input",
        "llext",
        "management",
        "meshcore",
        "message",
        "notify",
        "power",
        "radio",
        "telemetry",
    ] {
        let directory = root.join("include/meshbus/include").join(module);
        fs::create_dir_all(&directory).unwrap();
        fs::write(directory.join(format!("{module}.h")), "/* public */\n").unwrap();
    }
    for (include, headers) in [
        (
            "include/modules/lib/zui/include/zui",
            &[
                "zui.h",
                "core.h",
                "input.h",
                "predictive.h",
                "draw.h",
                "screen.h",
                "host.h",
                "toast.h",
                "components.h",
                "assets.h",
                "zui_types.h",
            ][..],
        ),
        (
            "include/modules/lib/u8g2/include/display",
            &["u8g2.h", "u8x8.h", "u8g2_snapshot.h"][..],
        ),
    ] {
        fs::create_dir_all(root.join(include)).unwrap();
        for header in headers {
            fs::write(root.join(include).join(header), "/* public */\n").unwrap();
        }
    }
}
