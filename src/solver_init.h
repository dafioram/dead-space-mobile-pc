// One supported-library initialization correction; no geometry diagnostics.
#pragma once

namespace solver {
// Called after HLE registration/library loading and before any guest execution.
// Unsupported symbols/instructions/stubs leave guest code unchanged.
bool install_joint_row_clear();
} // namespace solver
