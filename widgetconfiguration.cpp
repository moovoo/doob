#include "widgetconfiguration.h"
#include <QVariantMap>

QJsonObject widgetConfigurationToJson(const WidgetConfiguration& config) {
    return QJsonObject::fromVariantMap(config);
}


WidgetConfiguration widgetConfigurationFromJson(const QJsonObject& src) {
    return src.toVariantMap();
}
