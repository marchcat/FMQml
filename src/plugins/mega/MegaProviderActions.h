#pragma once
#include "FileActionPlugin.h"
QList<FileActionDescriptor> megaActionsForContext(const FileActionContext &context);
QVariantMap triggerMegaAction(const QString &actionId, const FileActionContext &context);
