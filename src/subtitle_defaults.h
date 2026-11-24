// Changes the guest's defaults, not a saved subtitle choice. Call before guest execution.
#pragma once

namespace subtitle_defaults {
bool install();  // false for an unrecognized library; leaves it unchanged
}
