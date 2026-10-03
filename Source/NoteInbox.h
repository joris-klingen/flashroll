#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace flashroll
{

// Lock-free single-producer / single-consumer hand-off of note-ons from the
// audio thread (processBlock) to the message thread (the processor's drill
// timer). The audio thread never touches the Drill: it only pushes the key
// that was struck. A full ring drops the newest note — at 256 slots that only
// happens if the message thread is stalled for seconds, and a stale answer is
// worthless then anyway.

class NoteInbox
{
public:
    // Audio thread.
    void push (int midi, int velocity) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            slots[static_cast<std::size_t> (scope.startIndex1)] = { midi, velocity };
    }

    // Message thread. Calls fn (midi, velocity) for every pending note, oldest first.
    template <typename Fn>
    void drain (Fn&& fn)
    {
        const auto scope = fifo.read (fifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i)
            call (fn, scope.startIndex1 + i);
        for (int i = 0; i < scope.blockSize2; ++i)
            call (fn, scope.startIndex2 + i);
    }

private:
    struct Note { int midi = 0, velocity = 0; };

    template <typename Fn>
    void call (Fn& fn, int index)
    {
        const auto& n = slots[static_cast<std::size_t> (index)];
        fn (n.midi, n.velocity);
    }

    static constexpr int kSize = 256;
    juce::AbstractFifo fifo { kSize };
    std::array<Note, kSize> slots {};
};

}  // namespace flashroll
