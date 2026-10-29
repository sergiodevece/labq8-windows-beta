#include "parameters/StateMigration.h"

namespace labq8::params
{
namespace
{
    // Must match the (private) identifiers AudioProcessorValueTreeState uses
    // for its children; the state round-trip tests catch any divergence.
    const juce::Identifier paramType { "PARAM" };
    const juce::Identifier idProperty { "id" };
    const juce::Identifier valueProperty { "value" };
} // namespace

int migrateState (juce::ValueTree& tree, const juce::AudioProcessorValueTreeState& state)
{
    const int foundVersion = tree.getProperty (stateVersionProperty, 0);

    // Version-specific transformations (renames, range changes) go here,
    // keyed on foundVersion. 1 -> 2 and 2 -> 3 only add parameters, handled
    // below: b{n}_type defaults to Bell, which is exactly what v1/v2 played.

    for (auto* p : state.processor.getParameters())
    {
        const auto* parameter = dynamic_cast<const juce::RangedAudioParameter*> (p);

        if (parameter == nullptr || tree.getChildWithProperty (idProperty, parameter->paramID).isValid())
            continue;

        juce::ValueTree child (paramType);
        child.setProperty (idProperty, parameter->paramID, nullptr);
        child.setProperty (valueProperty, parameter->convertFrom0to1 (parameter->getDefaultValue()), nullptr);
        tree.appendChild (child, nullptr);
    }

    tree.setProperty (stateVersionProperty, currentStateVersion, nullptr);
    return foundVersion;
}
} // namespace labq8::params
