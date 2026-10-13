#include "common.h"

#include <unordered_set>
#include <vector>

#include "mg_frame.hpp"

PORT_OVERRIDE mgCFrame *mgCFrame::SearchFrame(char *name) {
    if (mgFrameNameComp(this->name, name) != 0) {
        return this;
    }

    std::vector<mgCFrame *>        pending;
    std::unordered_set<mgCFrame *> visited_frames;
    // A malformed child/brother graph can revisit a frame instead of forming a tree.
    visited_frames.insert(this);
    if (child != NULL) {
        pending.push_back(child);
    }

    const std::size_t visit_limit = frame_num > 0 ? static_cast<std::size_t>(frame_num) : 4096;
    std::size_t       visited = 0;
    while (!pending.empty() && visited < visit_limit) {
        mgCFrame *node = pending.back();
        pending.pop_back();
        if (node == NULL) {
            continue;
        }

        if (!visited_frames.insert(node).second) {
            continue;
        }

        if (mgFrameNameComp(node->name, name) != 0) {
            return node;
        }
        ++visited;

        if (node->brother != NULL) {
            pending.push_back(node->brother);
        }
        if (node->child != NULL) {
            pending.push_back(node->child);
        }
    }

    return NULL;
}
