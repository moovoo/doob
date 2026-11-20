#ifndef WIDGETCONFIGURATION_H
#define WIDGETCONFIGURATION_H

#include <QVariant>
#include <QVariantMap>
#include <QString>
#include <QHash>
#include <QJsonObject>

using WidgetConfiguration = QVariantMap;

QJsonObject widgetConfigurationToJson(const WidgetConfiguration& config);
WidgetConfiguration widgetConfigurationFromJson(const QJsonObject& src);

#endif // WIDGETCONFIGURATION_H
