#include "ui/ParameterWatcher.h"

namespace iaq8::ui
{
ParameterWatcher::ParameterWatcher (juce::AudioProcessor& p, std::function<void()> callback)
    : processor (p), onChange (std::move (callback))
{
    for (auto* parameter : processor.getParameters())
        parameter->addListener (this);
}

ParameterWatcher::~ParameterWatcher()
{
    for (auto* parameter : processor.getParameters())
        parameter->removeListener (this);
    cancelPendingUpdate();
}

void ParameterWatcher::handleAsyncUpdate()
{
    if (onChange)
        onChange();
}
} // namespace iaq8::ui
