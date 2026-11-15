// Read-only credits counter in the RIG, styled by the game's own node panel.
#pragma once

namespace sbin { class Database; }

namespace rig_credits {

// Only the loaded layout copy is changed; no assets or inventory are written.
bool patch_layout(sbin::Database& db);
bool install();
void layout_added();  // enable updates only after a successful in-memory patch

}  // namespace rig_credits
