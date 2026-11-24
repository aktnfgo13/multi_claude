// Signal chain view: Pre lane, optional parallel A/B lanes, Post lane.
// Click = select, double-click = bypass, drag = reorder / move between lanes, right-click = menu,
// "+" at the end of each lane = add from the effect library.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "plugin/PluginProcessor.h"

namespace mfx::ui {

class ChainView : public juce::Component {
public:
    explicit ChainView(plugin::Processor& p);
    void rebuild();
    void paint(juce::Graphics&) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    int selectedSlot() const { return selected_; }
    void setSelected(int slot);
    std::function<void(int)> onSelectionChanged;
    std::function<void()> onStructureChanged;

private:
    struct Block { int slot; Lane lane; int index; juce::Rectangle<float> r; };
    struct LaneRow { Lane lane; juce::Rectangle<float> r; juce::Rectangle<float> plus; };
    void layoutBlocks();
    const Block* hit(juce::Point<float> p) const;
    void showAddMenu(Lane lane);
    void showBlockMenu(const Block& b);
    bool dropTarget(juce::Point<float> p, Lane& lane, int& index) const;

    plugin::Processor& proc_;
    std::vector<Block> blocks_;
    std::vector<LaneRow> lanes_;
    int selected_ = -1;
    int dragSlot_ = -1;
    juce::Point<float> dragPos_;
    bool dragging_ = false;
};

} // namespace mfx::ui
