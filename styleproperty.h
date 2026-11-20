#ifndef STYLEPROPERTY_H
#define STYLEPROPERTY_H

#include <QString>

// One "x-" custom style property a custom-painted widget reads (see
// StyleManager): the record the style sheet editor's help renders, so
// a property's docs live with the widget that reads it, not in a
// separate manual.
struct StyleProperty
{
    // The property's name in the sheet ("x-bar-margin").
    QString name;
    // The widget that reads it (its title, "Spectrum" — the help's
    // section).
    QString owner;
    // What it does (the help's description line).
    QString description;
    // The coded default, as the sheet would carry it ("" for a derived
    // default — the description then says what it derives from).
    QString defaultValue;
    // The value's unit ("px", "color"; "" when none).
    QString unit;
    // The numeric range, when the property has one (max == 0: no
    // range — a non-numeric property).
    int min = 0;
    int max = 0;
};

#endif // STYLEPROPERTY_H
