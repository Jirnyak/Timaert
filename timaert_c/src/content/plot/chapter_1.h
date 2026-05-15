#pragma once

#include "events/logic_nodes.h"

#include <utility>

namespace sm::content {

inline constexpr const char* kChapter1NodeId = "plot_chapter_1";

inline LogicNode chapter_1_placeholder_node() {
    LogicNode n;
    n.id = kChapter1NodeId;
    n.label = "Chapter 1 (placeholder)";
    ConditionSlot c;
    c.isEvent = false;
    c.check = [](const EventBus&, const PlayerState&) {
        return false;
    };
    n.conditions.push_back(std::move(c));
    n.mask.push_back(1);
    n.next.push_back(kChapter1NodeId);
    n.tags.push_back("plot");
    return n;
}

inline void register_chapter_1_nodes(LogicNodeEngine& logic) {
    logic.add(chapter_1_placeholder_node());
}

} // namespace sm::content
