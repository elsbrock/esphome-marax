"""Force-enable LV_USE_CHART in ESPHome's generated lv_conf.h.

ESPHome's LVGL component (as of 2026.4) does not expose a `chart:` widget
type, so its config generator never adds `CHART` to the helpers.lv_uses
set and emits `#define LV_USE_CHART 0`. Our temperature chart is built
imperatively from C++ (`includes/chart_helpers.h`), so we need the LVGL
chart code compiled in either way.

`add_lv_use("CHART")` is the same hook the lvgl component's own widget
registrations use; calling it at module import time inserts CHART into
the set before the lvgl component's `to_code` runs.

There's no YAML content for this component — referencing it in the main
YAML (`lvgl_chart_enable:`) is what causes ESPHome to import this module
and run the side-effect.
"""
import esphome.config_validation as cv
from esphome.components.lvgl.helpers import add_lv_use

DEPENDENCIES = ["lvgl"]
CONFIG_SCHEMA = cv.Schema({})

# Side effect at import time — populates the LVGL component's "uses" set
# before its codegen reads it.
add_lv_use("CHART")


async def to_code(config):
    # Nothing to generate; the import-time call above is all we need.
    pass
