module carven:support.task.impl;

import :support.task;
import std;

auto ContinuationTaskLoop::resume_one() noexcept -> void {
    std::exchange(next, {}).resume();
}

auto ContinuationTaskFrame::detach() noexcept -> void {
    if (parent != nullptr) {
        parent->child = nullptr;
        parent = nullptr;
    }
}

auto ContinuationTaskFrame::destroy() noexcept -> void {
    const auto* boundary = parent;
    auto* frame = this;
    while (frame->child != nullptr) {
        frame = frame->child;
    }
    do {
        auto* parent = frame->parent;
        const auto handle = frame->handle;
        // A child may borrow its parent's locals. Clear its owner's handle while
        // that storage is still alive, then release frames from child to parent.
        *frame->owner = {};
        frame->detach();
        handle.destroy();
        frame = parent;
    } while (frame != boundary);
}
