#pragma once

#include "requesttypes.h"

namespace WorkspaceExchange {

// Reads only collection definitions; the current workspace and history are untouched on failure.
bool read(const QString &path, Workspace *workspace, QString *error = nullptr);
bool write(const QString &path, const Workspace &workspace, QString *error = nullptr);

// Imported collections and requests receive fresh IDs, including when importing our own export.
Workspace merged(const Workspace &current, const Workspace &incoming);

} // namespace WorkspaceExchange
